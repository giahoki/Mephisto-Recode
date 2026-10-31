#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>
#include "vendor/json.hpp"
#include "play-policy.h"
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct Settings {
    std::wstring engine;
    int time = 1500, threads = (int)std::clamp(std::thread::hardware_concurrency()>1?std::thread::hardware_concurrency()-1:1u,1u,8u), hash = 256, lines = 1, delay = 200, skill = 20;
    bool autoplay = false, puzzles = false, evaluation = true, threat = false, brain = false, coordinates = true;
    int theme = 0, moveTime = 180, thinkVariance = 900, moveVariance = 90;
    int side = 0; // board orientation, white, black
    std::string variant = "chess", startFen;
    bool legit=false;
};
struct State { json data; Clock::time_point received{}; unsigned long long revision = 0; };
std::mutex lock;
Settings settings;
State latest;
std::atomic<bool> running{true};
std::atomic<unsigned long long> armRevision{0};
std::atomic<unsigned long long> settingsRevision{0};
SOCKET listener = INVALID_SOCKET;
int serverPort=17864;
HWND windowHandle, statusEdit, engineEdit, timeEdit, threadsEdit, hashEdit, linesEdit, delayEdit, skillEdit, sideBox, autoplayBox, puzzleBox;
HWND evaluationBox, threatBox, brainBox, coordinatesBox, themeBox, moveTimeEdit, thinkVarianceEdit, moveVarianceEdit, boardView;
HWND variantBox,startFenEdit;
const std::vector<std::string> variants{"chess","fischerandom","crazyhouse","kingofthehill","3check","antichess","atomic","horde","racingkings"};
std::wstring baseDirectory, settingsFile;
std::string status = "Waiting for browser. Load the extension and refresh a chess page.";
std::string analysisText, lastPlayed, pendingPromotion, pendingSession, pendingPlacement;
std::string displayedBest,displayedThreat;
std::vector<std::string> displayedMoves;
unsigned long long displayedRevision=0;
Clock::time_point pendingSince{};
POINT calibratedTL{}, calibratedBR{};
bool hasTL = false, calibrated = false;
json calibrationReference;
json moveConfirmationRequest;

std::string utf8(const std::wstring& value) {
    int n = WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),out.data(),n,nullptr,nullptr); return out;
}
std::wstring wide(const std::string& value) {
    int n = MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0);
    std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),out.data(),n); return out;
}
void setStatus(const std::string& text) { std::lock_guard<std::mutex> guard(lock); status = text; }
void saveSettings() {
    Settings s; { std::lock_guard<std::mutex> guard(lock); s = settings; }
    json j = {{"engine",utf8(s.engine)}, {"time",s.time},{"threads",s.threads},{"hash",s.hash},
        {"lines",s.lines},{"delay",s.delay},{"skill",s.skill},{"side",s.side},{"puzzles",s.puzzles},
        {"evaluation",s.evaluation},{"threat",s.threat},{"brain",s.brain},{"coordinates",s.coordinates},
        {"theme",s.theme},{"moveTime",s.moveTime},{"thinkVariance",s.thinkVariance},{"moveVariance",s.moveVariance},
        {"variant",s.variant},{"startFen",s.startFen},{"legit",s.legit}};
    std::ofstream file(settingsFile); file << j.dump(2);
}
void loadSettings() {
    settings.engine = baseDirectory + L"\\engines\\stockfish-17\\stockfish.exe";
    if(GetFileAttributesW(settings.engine.c_str())==INVALID_FILE_ATTRIBUTES)settings.engine=baseDirectory+L"\\stockfish.exe";
    try {
        std::ifstream file(settingsFile); json j; file >> j;
        settings.engine = wide(j.value("engine",utf8(settings.engine)));
        settings.time = std::clamp(j.value("time",settings.time),50,30000);
        settings.threads = std::clamp(j.value("threads",settings.threads),1,128);
        settings.hash = std::clamp(j.value("hash",settings.hash),16,8192);
        settings.lines = std::clamp(j.value("lines",settings.lines),1,10);
        settings.delay = std::clamp(j.value("delay",settings.delay),0,10000);
        settings.skill = std::clamp(j.value("skill",20),0,20);
        settings.side = std::clamp(j.value("side",0),0,2);
        settings.puzzles = j.value("puzzles",false);
        settings.evaluation=j.value("evaluation",true);settings.threat=j.value("threat",false);settings.brain=j.value("brain",false);
        settings.coordinates=j.value("coordinates",true);settings.theme=std::clamp(j.value("theme",0),0,2);
        settings.moveTime=std::clamp(j.value("moveTime",settings.moveTime),40,2000);
        settings.thinkVariance=std::clamp(j.value("thinkVariance",settings.thinkVariance),0,5000);
        settings.moveVariance=std::clamp(j.value("moveVariance",settings.moveVariance),0,1000);
        settings.variant=j.value("variant",std::string("chess"));if(std::find(variants.begin(),variants.end(),settings.variant)==variants.end())settings.variant="chess";
        settings.startFen=j.value("startFen",std::string{});
        settings.legit=j.value("legit",false);
    } catch (...) {}
    // Autoplay is deliberately armed per launch from the desktop window.
}

class Engine {
    HANDLE input = nullptr, output = nullptr, process = nullptr;
    std::string buffer,capabilities;
public:
    ~Engine() { close(); }
    void close() {
        if (input) { DWORD written; WriteFile(input,"quit\n",5,&written,nullptr); CloseHandle(input); input = nullptr; }
        if (process) { if (WaitForSingleObject(process,500) != WAIT_OBJECT_0) TerminateProcess(process,1); CloseHandle(process); process = nullptr; }
        if (output) { CloseHandle(output); output = nullptr; } buffer.clear();capabilities.clear();
    }
    void send(const std::string& s) {
        DWORD written = 0; std::string line = s + "\n";
        if (!input || !WriteFile(input,line.data(),(DWORD)line.size(),&written,nullptr) || written != line.size()) throw std::runtime_error("Engine pipe closed");
    }
    bool read(std::string& line) {
        auto newline = buffer.find('\n');
        if (newline == std::string::npos) {
            DWORD available = 0;
            if (!PeekNamedPipe(output,nullptr,0,nullptr,&available,nullptr)) throw std::runtime_error("Engine exited");
            if (available) { char bytes[8192]; DWORD count; if (!ReadFile(output,bytes,std::min<DWORD>(available,sizeof(bytes)),&count,nullptr)) throw std::runtime_error("Engine read failed"); buffer.append(bytes,count); }
            newline = buffer.find('\n');
        }
        if (newline == std::string::npos) {
            if (process && WaitForSingleObject(process,0) == WAIT_OBJECT_0) throw std::runtime_error("Engine exited");
            return false;
        }
        line = buffer.substr(0,newline); buffer.erase(0,newline+1);
        if (!line.empty() && line.back() == '\r') line.pop_back(); return true;
    }
    void waitFor(const std::string& expected) {
        auto end = Clock::now()+15s;
        while (running && Clock::now()<end) { std::string line; if (read(line)) {if(line.rfind("option name ",0)==0)capabilities+=line+" \n";if(line == expected)return;} std::this_thread::sleep_for(5ms); }
        throw std::runtime_error("Engine handshake timed out");
    }
    void start(const Settings& s) {
        close(); SECURITY_ATTRIBUTES attr{sizeof(attr),nullptr,TRUE};
        HANDLE childInput = nullptr, childOutput = nullptr;
        if (!CreatePipe(&childInput,&input,&attr,0)) throw std::runtime_error("CreatePipe failed");
        if (!CreatePipe(&output,&childOutput,&attr,0)) { CloseHandle(childInput); close(); throw std::runtime_error("CreatePipe failed"); }
        SetHandleInformation(input,HANDLE_FLAG_INHERIT,0); SetHandleInformation(output,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = childInput; si.hStdOutput = childOutput; si.hStdError = childOutput;
        PROCESS_INFORMATION pi{}; std::wstring command = L"\"" + s.engine + L"\"";
        auto dir = s.engine.substr(0,s.engine.find_last_of(L"\\/"));
        BOOL ok = CreateProcessW(s.engine.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,dir.c_str(),&si,&pi);
        CloseHandle(childInput); CloseHandle(childOutput);
        if (!ok) { close(); throw std::runtime_error("Cannot start engine; check the executable path"); }
        process = pi.hProcess; CloseHandle(pi.hThread);
        send("uci"); waitFor("uciok");
        if(s.variant!="chess" && !(s.variant=="fischerandom" && capabilities.find("name UCI_Chess960 ")!=std::string::npos) &&
            capabilities.find("var "+s.variant+" ")==std::string::npos)throw std::runtime_error("Selected engine does not support this variant. Choose Fairy-Stockfish.");
        send("setoption name Threads value " + std::to_string(s.threads));
        send("setoption name Hash value " + std::to_string(s.hash));
        send("setoption name MultiPV value " + std::to_string(s.legit?3:s.lines));
        send("setoption name Skill Level value " + std::to_string(s.skill));
        send("setoption name UCI_Chess960 value " + std::string(s.variant=="fischerandom"?"true":"false"));
        send("setoption name UCI_Variant value " + s.variant);
        send("isready"); waitFor("readyok");
    }
};

bool extensionOrigin(const std::string& origin) {
    return origin.rfind("chrome-extension://",0)==0 || origin.rfind("moz-extension://",0)==0;
}
void serveClient(SOCKET client) {
    DWORD timeout = 1500; setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
    setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,(char*)&timeout,sizeof(timeout));
    std::string request; size_t headerEnd; char buffer[8192];
    while ((headerEnd=request.find("\r\n\r\n"))==std::string::npos) {
        int n = recv(client,buffer,sizeof(buffer),0); if (n<=0 || request.size()>16384) return; request.append(buffer,n);
    }
    std::istringstream headers(request.substr(0,headerEnd)); std::string method,path,version,line;
    headers >> method >> path >> version; std::getline(headers,line);
    std::map<std::string,std::string> fields;
    while (std::getline(headers,line)) {
        auto colon=line.find(':'); if (colon==std::string::npos) continue;
        std::string key=line.substr(0,colon), value=line.substr(colon+1);
        std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return (char)std::tolower(c);});
        while (!value.empty() && value.front()==' ') value.erase(0,1);
        if (!value.empty() && value.back()=='\r') value.pop_back(); fields[key]=value;
    }
    std::string body="{\"ok\":false}"; int code=400;
    auto reply = [&] {
        std::string response="HTTP/1.1 "+std::to_string(code)+" Result\r\nContent-Type: application/json\r\nConnection: close\r\nCache-Control: no-store\r\n";
        if (extensionOrigin(fields["origin"])) response+="Access-Control-Allow-Origin: "+fields["origin"]+"\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nAccess-Control-Allow-Private-Network: true\r\n";
        response+="Content-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
        size_t offset=0; while(offset<response.size()) { int n=send(client,response.data()+offset,(int)(response.size()-offset),0); if(n<=0)break; offset+=n; }
    };
    // Drain the bounded body before replying, otherwise Winsock may reset the
    // connection when a rejected request body arrives in a separate TCP packet.
    std::string payload=request.substr(headerEnd+4);
    try {
        size_t size=fields.count("content-length")?std::stoul(fields.at("content-length")):0;
        if(size>131072) { code=413;reply();return; }
        while(payload.size()<size) { int n=recv(client,buffer,sizeof(buffer),0);if(n<=0)return;payload.append(buffer,n); }
        payload.resize(size);
    } catch(...) {reply();return;}
    if (!extensionOrigin(fields["origin"])) { code=403; reply(); return; }
    if(path=="/status" && method=="GET") {
        std::lock_guard<std::mutex> guard(lock);
        std::string text=std::string(settings.autoplay?"AUTOPLAY ARMED":"Autoplay off")+" | "+
            (!latest.data.is_null() && Clock::now()-latest.received<1200ms?"Browser connected":"No fresh browser data")+"\n"+status+"\n";
        if(!latest.data.is_null())text+=latest.data.value("fen",std::string{})+"\n";
        if(!latest.data.is_null() && latest.revision==displayedRevision)text+=analysisText;
        code=200;body=json({{"text",text},{"moves",displayedMoves},{"legit",settings.legit},{"autoplay",settings.autoplay}}).dump();reply();return;
    }
    if (path!="/state") { code=404; reply(); return; }
    if (method=="OPTIONS") { code=200; reply(); return; }
    if (method!="POST" || fields["content-type"].find("application/json")!=0) { reply(); return; }
    try {
        json data=json::parse(payload);
        for (const auto* key : {"session","fen","placement","orientation","title","url"}) if(!data.at(key).is_string()) throw std::runtime_error("Invalid state");
        if (data["fen"].get<std::string>().find_first_of("\r\n")!=std::string::npos) throw std::runtime_error("Invalid FEN");
        if (data["orientation"]!="white" && data["orientation"]!="black") throw std::runtime_error("Invalid orientation");
        if (data.at("session").get<std::string>().size()>100 || data.at("title").get<std::string>().size()>2048) throw std::runtime_error("Invalid strings");
        auto seq=data.at("sequence").get<unsigned long long>();
        if(!data.at("promotion").is_array() || data["promotion"].size()>4) throw std::runtime_error("Invalid promotion");
        for(const auto* key : {"left","top","width","height"}) { double n=data.at(key).get<double>(); if(!std::isfinite(n) || std::abs(n)>100000) throw std::runtime_error("Invalid geometry"); }
        if(data["width"].get<double>()<100 || data["height"].get<double>()<100) throw std::runtime_error("Invalid board");
        data.at("reliable").get<bool>(); data.at("focused").get<bool>();
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!latest.data.is_null() && latest.data["session"]==data["session"] && latest.data["sequence"].get<unsigned long long>()>=seq) { code=409; }
            else {
                if(calibrated && !calibrationReference.is_null()) {
                    for(const auto* key:{"left","top","width","height","session"})if(data[key]!=calibrationReference[key]){
                        calibrated=false;hasTL=false;status="Board moved: coordinates will be detected automatically.";break;
                    }
                }
                bool changed=latest.data.is_null() || latest.data["fen"]!=data["fen"] || latest.data["session"]!=data["session"] || latest.data["url"]!=data["url"];
                latest.data=std::move(data); latest.received=Clock::now(); if(changed)latest.revision++;
                code=200;body=json({{"ok",true},{"variant",settings.variant},{"startFen",settings.startFen},{"confirmMove",moveConfirmationRequest}}).dump();
            }
        }
    } catch(...) { code=400; }
    reply();
}
void serverLoop() {
    WSADATA ws{}; if(WSAStartup(MAKEWORD(2,2),&ws)!=0) { setStatus("Network initialization failed");return; }
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    BOOL exclusive=TRUE; setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(char*)&exclusive,sizeof(exclusive));
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons((u_short)serverPort);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(listener,(sockaddr*)&addr,sizeof(addr)) || listen(listener,8)) {setStatus("Port 17864 is busy. Close another Mephisto instance.");closesocket(listener);listener=INVALID_SOCKET;WSACleanup();return;}
    while(running) {
        fd_set reads;FD_ZERO(&reads);FD_SET(listener,&reads);timeval timeout{0,250000};
        if(select(0,&reads,nullptr,nullptr,&timeout)>0) { SOCKET client=accept(listener,nullptr,nullptr);if(client!=INVALID_SOCKET){serveClient(client);closesocket(client);} }
    }
    closesocket(listener);listener=INVALID_SOCKET;WSACleanup();
}

bool fresh(const State& s) { return !s.data.is_null() && Clock::now()-s.received<1200ms; }
bool browserInFront(const json& data) {
    HWND fg=GetForegroundWindow(); if(!fg || fg==windowHandle)return false;
    wchar_t title[4096]{};GetWindowTextW(fg,title,4096);
    std::wstring expected=wide(data.value("title",std::string{}));
    if(expected.empty() || std::wstring(title).find(expected)==std::wstring::npos)return false;
    wchar_t cls[256]{};GetClassNameW(fg,cls,256);
    return std::wstring(cls)==L"Chrome_WidgetWin_1" || std::wstring(cls)==L"MozillaWindowClass";
}
bool clickAt(double x,double y,const json& data,double clientX=std::nan(""),double clientY=std::nan("")) {
    {std::lock_guard<std::mutex> guard(lock);if(!settings.autoplay)return false;}
    if(!std::isfinite(x)||!std::isfinite(y)||!browserInFront(data) || GetAsyncKeyState(VK_ESCAPE)&0x8000) return false;
    if(!std::isfinite(clientX)||!std::isfinite(clientY)||!data.contains("scale")) {
        setStatus("Обновите аддон: для кликов нужны подтверждённые координаты курсора.");return false;
    }
    POINT p{};bool verified=false;
    for(int attempt=0;attempt<3 && running;attempt++) {
        if(!browserInFront(data))return false;
        p={(LONG)std::lround(x),(LONG)std::lround(y)};
        HWND under=WindowFromPoint(p);if(!under||GetAncestor(under,GA_ROOT)!=GetForegroundWindow())return false;
        unsigned long long pointerBefore=0;
        {std::lock_guard<std::mutex> guard(lock);if(!settings.autoplay)return false;
            if(latest.data.contains("pointer")&&latest.data["pointer"].is_object())pointerBefore=latest.data["pointer"].value("sequence",0ull);}
        SetCursorPos(p.x+1,p.y);std::this_thread::sleep_for(12ms);
        if(!SetCursorPos(p.x,p.y))return false;
        auto deadline=Clock::now()+1100ms;json observed;
        while(running&&Clock::now()<deadline) {
            {std::lock_guard<std::mutex> guard(lock);
                if(!settings.autoplay||latest.data["session"]!=data["session"]||latest.data["fen"]!=data["fen"])return false;
                if(latest.data.contains("pointer")&&latest.data["pointer"].is_object()&&latest.data["pointer"].value("sequence",0ull)>pointerBefore){observed=latest.data["pointer"];break;}}
            if(!browserInFront(data))return false;
            std::this_thread::sleep_for(10ms);
        }
        POINT current{};GetCursorPos(&current);
        if(current.x!=p.x||current.y!=p.y)return false; // User moved the mouse: yield immediately.
        if(observed.is_null()){setStatus("Нет подтверждения курсора от страницы. Проверьте аддон и калибровку automatic coordinate detection.");return false;}
        double dx=clientX-observed.at("x").get<double>(),dy=clientY-observed.at("y").get<double>();
        if(std::abs(dx)<=2.0&&std::abs(dy)<=2.0){verified=true;break;}
        x+=dx*data.at("scale").get<double>();y+=dy*data.at("scale").get<double>();
    }
    if(!verified){setStatus("Координаты не совпали с клеткой: клик отменён. Выполните калибровку automatic coordinate detection.");return false;}
    {std::lock_guard<std::mutex> guard(lock);
        if(!settings.autoplay)return false;
        const double scale=data.at("scale");
        calibratedTL={(LONG)std::lround(p.x-(clientX-data.at("clientLeft").get<double>())*scale),(LONG)std::lround(p.y-(clientY-data.at("clientTop").get<double>())*scale)};
        calibratedBR={calibratedTL.x+(LONG)std::lround(data.at("width").get<double>()),calibratedTL.y+(LONG)std::lround(data.at("height").get<double>())};
        calibrated=true;calibrationReference=data;
    }
    if(!browserInFront(data))return false;
    HWND under=WindowFromPoint(p);if(!under||GetAncestor(under,GA_ROOT)!=GetForegroundWindow())return false;
    INPUT input{}; input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    if(SendInput(1,&input,sizeof(input))!=1)return false;
    std::this_thread::sleep_for(65ms);input.mi.dwFlags=MOUSEEVENTF_LEFTUP;return SendInput(1,&input,sizeof(input))==1;
}
std::pair<double,double> squareClientCenter(const std::string& square,const json& data) {
    if(!data.contains("clientLeft"))return {std::nan(""),std::nan("")};
    int f=square[0]-'a',r=square[1]-'1';bool black=data["orientation"]=="black";
    return {data.at("clientLeft").get<double>()+((black?7-f:f)+0.5)*data.at("clientWidth").get<double>()/8,
        data.at("clientTop").get<double>()+((black?r:7-r)+0.5)*data.at("clientHeight").get<double>()/8};
}
std::pair<double,double> squareCenter(const std::string& square,const json& data) {
    double left=data["left"],top=data["top"],w=data["width"],h=data["height"];
    { std::lock_guard<std::mutex> guard(lock); if(calibrated){left=calibratedTL.x;top=calibratedTL.y;w=calibratedBR.x-left;h=calibratedBR.y-top;} }
    int file=square[0]-'a',rank=square[1]-'1';bool black=data["orientation"]=="black";
    return {left+((black?7-file:file)+0.5)*w/8,top+((black?rank:7-rank)+0.5)*h/8};
}
bool stillCurrent(const State& s,unsigned long long version) {
    std::lock_guard<std::mutex> guard(lock);
    if(!settings.autoplay || settingsRevision.load()!=version || !fresh(latest) || !latest.data.value("focused",false) || latest.revision!=s.revision)return false;
    for(const auto* key : {"left","top","width","height","orientation"}) if(latest.data[key]!=s.data[key])return false;
    return true;
}
void workerLoop() {
    Engine engine;std::string engineKey;unsigned long long analyzed=0,analyzedSettingsVersion=0;int analyzedTime=0;
    std::string bestMove;unsigned long long lastArmRevision=0;bool previousBrain=false,previousEval=true,previousThreat=true;
    std::string awaitingPlacement,awaitingSession,awaitingMove,awaitingId;size_t awaitingHistoryIndex=0;Clock::time_point clickedAt{};
    std::mt19937 random(std::random_device{}());std::vector<play::Candidate> candidates;
    std::string attemptKey;int attempts=0;bool sourceSelected=false;Clock::time_point retryAt=Clock::time_point::max();
    while(running) {
        try {
            State s;Settings cfg;unsigned long long version;
            {std::lock_guard<std::mutex> guard(lock);s=latest;cfg=settings;version=settingsRevision.load();}
            auto currentArmRevision=armRevision.load();if(currentArmRevision!=lastArmRevision){lastPlayed.clear();awaitingPlacement.clear();pendingPromotion.clear();attempts=0;retryAt=Clock::time_point::max();{std::lock_guard<std::mutex> guard(lock);moveConfirmationRequest=nullptr;}lastArmRevision=currentArmRevision;}
            if(!awaitingPlacement.empty()) {
                if(!cfg.autoplay || (!s.data.is_null() && s.data["session"]!=awaitingSession))awaitingPlacement.clear();
                else if(fresh(s) && s.data["promotion"].empty() &&
                    ((s.data.contains("confirmation")&&s.data["confirmation"].is_object()&&s.data["confirmation"].value("id",std::string{})==awaitingId&&s.data["confirmation"].value("placement",std::string{})==s.data.value("placement",std::string{})) ||
                    (s.data.contains("historyLan")&&s.data["historyLan"].is_array()&&s.data["historyLan"].size()>awaitingHistoryIndex&&s.data["historyLan"][awaitingHistoryIndex].get<std::string>()==awaitingMove))) {
                    awaitingPlacement.clear();{std::lock_guard<std::mutex> guard(lock);moveConfirmationRequest=nullptr;}setStatus("Ход подтверждён позицией или историей ходов.");
                } else if(Clock::now()-clickedAt>5s) {
                    awaitingPlacement.clear();pendingPromotion.clear();
                    {std::lock_guard<std::mutex> guard(lock);moveConfirmationRequest=nullptr;}
                    retryAt=Clock::now()+std::chrono::milliseconds(std::uniform_int_distribution<int>(900,1800)(random));
                    setStatus(attempts<3?"Ход не подтверждён. Проверяем позицию и готовим повторную попытку.":"Три попытки без подтверждения. Автоплей включён; проверьте калибровку automatic coordinate detection.");continue;
                }
            }
            if(!fresh(s)){std::this_thread::sleep_for(100ms);continue;}
            if(s.data.value("variant",std::string("chess"))!=cfg.variant || s.data.value("startFen",std::string{})!=cfg.startFen){setStatus("Synchronizing variant / starting position with browser...");std::this_thread::sleep_for(100ms);continue;}
            if(previousBrain!=cfg.brain || previousEval!=cfg.evaluation || previousThreat!=cfg.threat){analyzed=0;previousBrain=cfg.brain;previousEval=cfg.evaluation;previousThreat=cfg.threat;}
            if(!pendingPromotion.empty()) {
                if(!cfg.autoplay || Clock::now()-pendingSince>4s || s.data["session"]!=pendingSession) {
                    pendingPromotion.clear();setStatus("Promotion was not confirmed. Check the board.");
                } else {
                    for(const auto& p:s.data["promotion"]) if(p.value("type",std::string{})==pendingPromotion) {
                        if(clickAt(p.at("x").get<double>(),p.at("y").get<double>(),s.data,p.value("clientX",std::nan("")),p.value("clientY",std::nan("")))){pendingPromotion.clear();setStatus("Выбор превращения отправлен; ожидаем подтверждение хода.");}break;
                    }
                    if(s.data["placement"]!=pendingPlacement && s.data["promotion"].empty()) pendingPromotion.clear();
                }
                std::this_thread::sleep_for(50ms);continue;
            }
            std::string key=utf8(cfg.engine)+"/"+std::to_string(cfg.threads)+"/"+std::to_string(cfg.hash)+"/"+std::to_string(cfg.legit?3:cfg.lines)+"/"+std::to_string(cfg.skill)+"/"+cfg.variant;
            if(key!=engineKey){engine.start(cfg);engineKey=key;analyzed=0;}
            if(s.data.value("fen",std::string{}).empty()){setStatus("Cannot determine turn. For Chess960/custom games, supply the starting FEN or open the starting position.");std::this_thread::sleep_for(100ms);continue;}
            if(analyzed!=s.revision || analyzedTime!=cfg.time || analyzedSettingsVersion!=version) {
                engine.send("position fen "+s.data["fen"].get<std::string>());
                engine.send("go movetime "+std::to_string(cfg.time));
                auto deadline=Clock::now()+std::chrono::milliseconds(cfg.time)+5s;
                bool canceled=false,done=false; std::map<int,std::string> lines;bestMove.clear();
                setStatus("Analyzing current position...");
                while(running && Clock::now()<deadline) {
                    {std::lock_guard<std::mutex> guard(lock);if((latest.revision!=s.revision || settingsRevision.load()!=version || !fresh(latest)) && !canceled){engine.send("stop");canceled=true;}}
                    std::string line;
                    if(!engine.read(line)){std::this_thread::sleep_for(5ms);continue;}
                    if(line.rfind("info depth ",0)==0 && line.find(" pv ")!=std::string::npos) {
                        int index=1;auto at=line.find(" multipv ");if(at!=std::string::npos)index=std::stoi(line.substr(at+9));
                        lines[index]=line.substr(5);
                    }
                    if(line.rfind("bestmove ",0)==0){std::istringstream parts(line);std::string ignored;parts>>ignored>>bestMove;done=true;break;}
                }
                if(!done) {engine.close();engineKey.clear();throw std::runtime_error("Engine analysis timed out");}
                if(canceled){analyzed=0;continue;}
                analyzed=s.revision;analyzedTime=cfg.time;analyzedSettingsVersion=version;
                candidates.clear();for(const auto& entry:lines){auto c=play::parse(entry.second);if(!c.move.empty())candidates.push_back(c);if(candidates.size()==3)break;}
                int pieces=0;for(char p:s.data.value("placement",std::string{}))if(std::isalpha((unsigned char)p))pieces++;
                if(cfg.legit&&!candidates.empty())bestMove=candidates[play::choose(candidates,pieces,random)].move;
                std::string display,reply;
                if(lines.count(1)){auto pv=lines[1].find(" pv ");if(pv!=std::string::npos){std::istringstream moves(lines[1].substr(pv+4));std::string first;moves>>first>>reply;}}
                for(const auto& pair:lines) {
                    auto pv=pair.second.find(" pv ");std::string value=pair.second;
                    if(!cfg.evaluation && pv!=std::string::npos)value="PV "+pair.second.substr(pv+4);
                    display+=std::to_string(pair.first)+". "+value+"\r\n\r\n";
                }
                std::string heading=(cfg.legit?"Selected move: ":"Best move: ")+bestMove;
                if(cfg.brain && bestMove.size()>=2) {
                    std::string placement=s.data["placement"];int file=0,rank=7;char piece='?';
                    for(char ch:placement){if(ch=='/'){rank--;file=0;}else if(ch>='1'&&ch<='8')file+=ch-'0';else {if(file==bestMove[0]-'a' && rank==bestMove[1]-'1')piece=(char)std::toupper((unsigned char)ch);file++;}}
                    std::map<char,std::string> names{{'P',"Pawn"},{'N',"Knight"},{'B',"Bishop"},{'R',"Rook"},{'Q',"Queen"},{'K',"King"}};
                    heading="Hand & Brain: "+names[piece];display.clear();
                }
                if(cfg.threat && !reply.empty())heading+="\r\nExpected reply: "+reply;
                const auto fenNow=s.data["fen"].get<std::string>();const auto turnNow=fenNow.substr(fenNow.find(' ')+1,1);
                const std::string ownSide=cfg.side==1?"w":cfg.side==2?"b":s.data["orientation"]=="white"?"w":"b";
                {std::lock_guard<std::mutex> guard(lock);analysisText=heading+"\r\n\r\n"+display;displayedBest=cfg.brain||turnNow!=ownSide?"":bestMove;displayedMoves.clear();if(!displayedBest.empty()){if(cfg.legit)for(const auto& c:candidates)displayedMoves.push_back(c.move);else displayedMoves.push_back(bestMove);}displayedThreat="";displayedRevision=s.revision;}
                setStatus(s.data.value("reliable",false)?"Position verified against move history.":"Piece-only position: castling/en passant unknown. Enable puzzle mode to autoplay.");
            }
            std::string playedKey=s.data["session"].get<std::string>()+s.data["fen"].get<std::string>();
            if(attemptKey!=playedKey){attemptKey=playedKey;attempts=0;sourceSelected=false;retryAt=Clock::time_point::max();}
            if(lastPlayed==playedKey&&awaitingPlacement.empty()&&Clock::now()>=retryAt&&play::retryAllowed(attempts,true,fresh(s),s.data.value("focused",false))&&browserInFront(s.data)){
                lastPlayed.clear();retryAt=Clock::time_point::max();setStatus("Повторная попытка: "+std::to_string(attempts+1)+" из 3.");
            }
            bool drop=bestMove.size()==4 && bestMove[1]=='@' && std::string("PNBRQpnbrq").find(bestMove[0])!=std::string::npos;
            if(cfg.autoplay && awaitingPlacement.empty() && playedKey!=lastPlayed && bestMove.size()>=4 && bestMove.size()<=5 &&
                (drop || (bestMove[0]>='a' && bestMove[0]<='h' && bestMove[1]>='1' && bestMove[1]<='8')) &&
                bestMove[2]>='a' && bestMove[2]<='h' && bestMove[3]>='1' && bestMove[3]<='8' &&
                (s.data.value("reliable",false)||cfg.puzzles)) {
                auto fen=s.data["fen"].get<std::string>();auto turn=fen.substr(fen.find(' ')+1,1);
                std::string side=cfg.side==1?"w":cfg.side==2?"b":s.data["orientation"]=="white"?"w":"b";
                if(turn!=side){setStatus("Автоплей готов: ожидаем ход соперника.");std::this_thread::sleep_for(100ms);continue;}
                if(!browserInFront(s.data)){setStatus("Автоплей готов: вернитесь в браузер и откройте доску полностью.");std::this_thread::sleep_for(100ms);continue;}
                auto variation=[&](int maximum){return maximum?std::uniform_int_distribution<int>(0,maximum)(random):0;};
                int pieces=0;for(char p:s.data.value("placement",std::string{}))if(std::isalpha((unsigned char)p))pieces++;
                int waitMs=cfg.legit?play::delay(candidates,pieces,s.data.value("legalCount",0),s.data.value("clockSeconds",-1.0),cfg.time,random):cfg.delay+variation(cfg.thinkVariance);
                if(attempts>0)waitMs=250+variation(400);
                auto until=Clock::now()+std::chrono::milliseconds(waitMs);
                while(running && Clock::now()<until && stillCurrent(s,version))std::this_thread::sleep_for(10ms);
                if(!running || !stillCurrent(s,version))continue;
                auto from=drop?std::make_pair(0.0,0.0):squareCenter(bestMove.substr(0,2),s.data),to=squareCenter(bestMove.substr(2,2),s.data);
                auto fromClient=drop?std::make_pair(std::nan(""),std::nan("")):squareClientCenter(bestMove.substr(0,2),s.data),toClient=squareClientCenter(bestMove.substr(2,2),s.data);
                if(drop){
                    bool found=false;
                    if(s.data.contains("pockets"))for(const auto& pocket:s.data["pockets"]){
                        if(pocket.value("type",std::string{})==std::string(1,(char)std::tolower((unsigned char)bestMove[0])) && pocket.value("color",std::string{})==turn){from={pocket.at("x").get<double>(),pocket.at("y").get<double>()};fromClient={pocket.value("clientX",std::nan("")),pocket.value("clientY",std::nan(""))};found=true;break;}
                    }
                    if(!found){setStatus("Cannot locate the pocket piece for this drop move. Check the active board.");std::this_thread::sleep_for(100ms);continue;}
                }
                bool destinationOnly=sourceSelected&&attempts==1;
                attempts++;
                if(destinationOnly||clickAt(from.first,from.second,s.data,fromClient.first,fromClient.second)) {
                    sourceSelected=true;
                    lastPlayed=playedKey;
                    auto between=Clock::now()+std::chrono::milliseconds(std::max(100,cfg.moveTime)+variation(std::max(60,cfg.moveVariance)));
                    while(running && Clock::now()<between && stillCurrent(s,version))std::this_thread::sleep_for(5ms);
                    to=squareCenter(bestMove.substr(2,2),s.data);
                    if(stillCurrent(s,version) && clickAt(to.first,to.second,s.data,toClient.first,toClient.second)) {
                        awaitingPlacement=s.data["placement"];awaitingSession=s.data["session"];clickedAt=Clock::now();
                        awaitingMove=bestMove;awaitingHistoryIndex=s.data.value("moves",json::array()).size();awaitingId=std::to_string(GetTickCount64());
                        {std::lock_guard<std::mutex> guard(lock);moveConfirmationRequest={{"id",awaitingId},{"move",bestMove},{"fen",s.data["fen"]},{"variant",cfg.variant}};}
                        if(bestMove.size()==5){pendingPromotion=bestMove.substr(4);pendingSession=s.data["session"];pendingPlacement=s.data["placement"];pendingSince=Clock::now();}
                        setStatus("Ввод отправлен. Ход ещё не подтверждён — ожидаем доску/историю.");
                    } else {retryAt=Clock::now()+1200ms;setStatus("Ввод прерван. Повторим после проверки позиции и фокуса.");}
                } else {lastPlayed=playedKey;retryAt=Clock::now()+1500ms;setStatus(attempts<3?"Клетка не подтверждена: повторим попытку после проверки координат.":"Три попытки без подтверждения клетки. Проверьте automatic coordinate detection и обновление аддона.");}
            }
            std::this_thread::sleep_for(50ms);
        } catch(const std::exception& e) {setStatus(std::string("Error: ")+e.what());engine.close();engineKey.clear();std::this_thread::sleep_for(500ms);}
    }
}

#ifdef MEPHISTO_LEGACY_UI
std::wstring controlText(HWND control) {int n=GetWindowTextLengthW(control);std::wstring s(n+1,0);GetWindowTextW(control,s.data(),n+1);s.resize(n);return s;}
int number(HWND control,int low,int high,int fallback) {try{return std::clamp(std::stoi(controlText(control)),low,high);}catch(...){return fallback;}}
HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    HWND item=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,windowHandle,(HMENU)(INT_PTR)id,nullptr,nullptr);
    SendMessageW(item,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);return item;
}
void applyControls() {
    {std::lock_guard<std::mutex> guard(lock);
        settings.engine=controlText(engineEdit);settings.time=number(timeEdit,50,30000,1000);
        settings.threads=number(threadsEdit,1,128,2);settings.hash=number(hashEdit,16,8192,128);
        settings.lines=number(linesEdit,1,10,2);settings.delay=number(delayEdit,0,10000,250);settings.skill=number(skillEdit,0,20,20);
        settings.side=(int)SendMessageW(sideBox,CB_GETCURSEL,0,0);
        settings.puzzles=SendMessageW(puzzleBox,BM_GETCHECK,0,0)==BST_CHECKED;
        bool armed=SendMessageW(autoplayBox,BM_GETCHECK,0,0)==BST_CHECKED;
        settings.evaluation=SendMessageW(evaluationBox,BM_GETCHECK,0,0)==BST_CHECKED;
        settings.threat=SendMessageW(threatBox,BM_GETCHECK,0,0)==BST_CHECKED;
        settings.brain=SendMessageW(brainBox,BM_GETCHECK,0,0)==BST_CHECKED;
        settings.coordinates=SendMessageW(coordinatesBox,BM_GETCHECK,0,0)==BST_CHECKED;
        settings.theme=(int)SendMessageW(themeBox,CB_GETCURSEL,0,0);
        settings.moveTime=number(moveTimeEdit,40,2000,100);settings.thinkVariance=number(thinkVarianceEdit,0,5000,0);settings.moveVariance=number(moveVarianceEdit,0,1000,0);
        int variantIndex=(int)SendMessageW(variantBox,CB_GETCURSEL,0,0);settings.variant=variants[std::clamp(variantIndex,0,(int)variants.size()-1)];
        settings.startFen=utf8(controlText(startFenEdit));
        if(armed!=settings.autoplay)armRevision++;settings.autoplay=armed;settingsRevision++;
    }
    saveSettings();
}
void drawBoard(const DRAWITEMSTRUCT& draw) {
    State s;Settings cfg;std::string best,threat;unsigned long long revision;
    {std::lock_guard<std::mutex> guard(lock);s=latest;cfg=settings;best=displayedBest;threat=displayedThreat;revision=displayedRevision;}
    HDC dc=draw.hDC;int padding=cfg.coordinates?20:0;
    int size=std::min(draw.rcItem.right,draw.rcItem.bottom)-padding*2,cell=size/8;size=cell*8;
    bool black=!s.data.is_null() && s.data["orientation"]=="black";
    COLORREF light[]={RGB(240,217,181),RGB(235,236,208),RGB(220,225,233)};
    COLORREF dark[]={RGB(181,136,99),RGB(119,149,86),RGB(102,123,153)};
    FillRect(dc,&draw.rcItem,(HBRUSH)(COLOR_WINDOW+1));
    HFONT font=CreateFontW(cell-6,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI Symbol");
    HGDIOBJ oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);
    for(int row=0;row<8;row++)for(int col=0;col<8;col++){
        RECT r{padding+col*cell,padding+row*cell,padding+(col+1)*cell,padding+(row+1)*cell};
        HBRUSH brush=CreateSolidBrush((row+col)%2?dark[cfg.theme]:light[cfg.theme]);FillRect(dc,&r,brush);DeleteObject(brush);
    }
    if(!s.data.is_null()) {
        int file=0,rank=7;
        std::string pieces="KQRBNPkqrbnp";std::wstring symbols=L"\u2654\u2655\u2656\u2657\u2658\u2659\u265a\u265b\u265c\u265d\u265e\u265f";
        for(char ch:s.data.value("placement",std::string{})) {
            if(ch=='/'){rank--;file=0;continue;}if(ch>='1'&&ch<='8'){file+=ch-'0';continue;}
            auto at=pieces.find(ch);if(at!=std::string::npos && file<8 && rank>=0 && rank<8) {
                int col=black?7-file:file,row=black?rank:7-rank;RECT r{padding+col*cell,padding+row*cell,padding+(col+1)*cell,padding+(row+1)*cell};
                wchar_t symbol[]={symbols[at],0};SetTextColor(dc,RGB(15,20,27));DrawTextW(dc,symbol,1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }file++;
        }
    }
    auto arrow=[&](const std::string& move,COLORREF color) {
        if(move.size()<4 || move[0]<'a'||move[0]>'h'||move[1]<'1'||move[1]>'8'||move[2]<'a'||move[2]>'h'||move[3]<'1'||move[3]>'8')return;
        auto point=[&](int f,int r){return POINT{padding+(black?7-f:f)*cell+cell/2,padding+(black?r:7-r)*cell+cell/2};};
        POINT a=point(move[0]-'a',move[1]-'1'),b=point(move[2]-'a',move[3]-'1');
        HPEN pen=CreatePen(PS_SOLID,4,color);auto old=SelectObject(dc,pen);MoveToEx(dc,a.x,a.y,nullptr);LineTo(dc,b.x,b.y);
        double angle=std::atan2((double)b.y-a.y,(double)b.x-a.x);
        for(double delta:{-0.55,0.55}){MoveToEx(dc,b.x,b.y,nullptr);LineTo(dc,b.x-(int)(14*std::cos(angle+delta)),b.y-(int)(14*std::sin(angle+delta)));}
        SelectObject(dc,old);DeleteObject(pen);
    };
    if(fresh(s) && s.revision==revision){arrow(best,RGB(30,85,220));arrow(threat,RGB(200,50,45));}
    SelectObject(dc,oldFont);DeleteObject(font);
    if(cfg.coordinates){
        SetTextColor(dc,RGB(50,50,50));for(int i=0;i<8;i++){
            wchar_t file=(wchar_t)(black?'h'-i:'a'+i),rank=(wchar_t)(black?'1'+i:'8'-i);
            TextOutW(dc,padding+i*cell+cell/2-3,padding+size+3,&file,1);TextOutW(dc,4,padding+i*cell+cell/2-6,&rank,1);
        }
    }
}
LRESULT CALLBACK wndProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_CREATE: {
        windowHandle=hwnd;
        control(L"STATIC",L"MEPHISTO  /  Desktop engine + mouse autoplay",0,20,15,710,24);
        control(L"STATIC",L"UCI engine",0,20,52,90,20);
        engineEdit=control(L"EDIT",settings.engine.c_str(),ES_AUTOHSCROLL|WS_TABSTOP,110,48,545,26);
        control(L"BUTTON",L"Browse...",WS_TABSTOP,665,47,100,28,101);
        const wchar_t* labels[]={L"Analysis ms",L"Threads",L"Hash MB",L"PV lines",L"Click delay ms",L"Skill 0-20"};
        int values[]={settings.time,settings.threads,settings.hash,settings.lines,settings.delay,settings.skill};
        HWND* edits[]={&timeEdit,&threadsEdit,&hashEdit,&linesEdit,&delayEdit,&skillEdit};
        for(int i=0;i<6;i++){control(L"STATIC",labels[i],0,20+i*125,90,120,20);*edits[i]=control(L"EDIT",std::to_wstring(values[i]).c_str(),ES_NUMBER|WS_TABSTOP,20+i*125,113,110,26);}
        control(L"STATIC",L"Play side",0,20,155,75,20);
        sideBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,100,151,170,120);
        for(const auto* text:{L"Board orientation",L"White",L"Black"})SendMessageW(sideBox,CB_ADDSTRING,0,(LPARAM)text);
        SendMessageW(sideBox,CB_SETCURSEL,settings.side,0);
        autoplayBox=control(L"BUTTON",L"Autoplay (real mouse)",BS_AUTOCHECKBOX|WS_TABSTOP,290,150,200,28,103);
        puzzleBox=control(L"BUTTON",L"Puzzle / piece-only mode",BS_AUTOCHECKBOX|WS_TABSTOP,500,150,240,28,104);
        SendMessageW(puzzleBox,BM_SETCHECK,settings.puzzles?BST_CHECKED:BST_UNCHECKED,0);
        control(L"BUTTON",L"Apply settings",WS_TABSTOP,20,190,140,32,102);
        control(L"BUTTON",L"Reset calibration",WS_TABSTOP,175,190,155,32,105);
        control(L"STATIC",L"F8: board top-left   F9: bottom-right   F10: STOP\r\nFocus the browser to play. Keep the board fully visible. Popup may stay closed.",0,350,185,425,42);
        evaluationBox=control(L"BUTTON",L"Evaluation",BS_AUTOCHECKBOX|WS_TABSTOP,20,238,110,25,104);
        threatBox=control(L"BUTTON",L"Threat / reply",BS_AUTOCHECKBOX|WS_TABSTOP,140,238,125,25,104);
        brainBox=control(L"BUTTON",L"Hand & Brain",BS_AUTOCHECKBOX|WS_TABSTOP,280,238,135,25,104);
        coordinatesBox=control(L"BUTTON",L"Coordinates",BS_AUTOCHECKBOX|WS_TABSTOP,430,238,120,25,104);
        HWND boxes[]={evaluationBox,threatBox,brainBox,coordinatesBox};bool states[]={settings.evaluation,settings.threat,settings.brain,settings.coordinates};
        for(int i=0;i<4;i++)SendMessageW(boxes[i],BM_SETCHECK,states[i]?BST_CHECKED:BST_UNCHECKED,0);
        themeBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,565,238,125,100);
        for(const auto* name:{L"Brown board",L"Green board",L"Blue board"})SendMessageW(themeBox,CB_ADDSTRING,0,(LPARAM)name);
        SendMessageW(themeBox,CB_SETCURSEL,settings.theme,0);
        control(L"STATIC",L"Between clicks ms",0,20,277,145,20);moveTimeEdit=control(L"EDIT",std::to_wstring(settings.moveTime).c_str(),ES_NUMBER|WS_TABSTOP,165,273,70,26);
        control(L"STATIC",L"Think variance ms",0,250,277,135,20);thinkVarianceEdit=control(L"EDIT",std::to_wstring(settings.thinkVariance).c_str(),ES_NUMBER|WS_TABSTOP,390,273,70,26);
        control(L"STATIC",L"Click variance ms",0,475,277,130,20);moveVarianceEdit=control(L"EDIT",std::to_wstring(settings.moveVariance).c_str(),ES_NUMBER|WS_TABSTOP,615,273,75,26);
        control(L"BUTTON",L"Open analysis board",WS_TABSTOP,20,310,180,28,106);
        control(L"STATIC",L"Variant",0,230,315,65,20);variantBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,295,310,190,220);
        for(const auto& variant:variants)SendMessageW(variantBox,CB_ADDSTRING,0,(LPARAM)wide(variant).c_str());
        auto selected=std::find(variants.begin(),variants.end(),settings.variant)-variants.begin();SendMessageW(variantBox,CB_SETCURSEL,selected,0);
        control(L"STATIC",L"Blue: best move    Red: expected reply",0,790,15,400,24);
        boardView=control(L"STATIC",L"",SS_OWNERDRAW,790,48,420,420,200);
        control(L"STATIC",L"Starting FEN (optional; custom setup / Chess960)",0,790,490,420,22);
        startFenEdit=control(L"EDIT",wide(settings.startFen).c_str(),ES_AUTOHSCROLL|WS_TABSTOP,790,520,420,26);
        control(L"STATIC",L"Select a native UCI engine supporting the variant.\r\nChess960: capture the initial board or enter its FEN.\r\nUse Apply settings after changing a variant or FEN.",0,790,560,420,70);
        statusEdit=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL,20,350,745,335);
        RegisterHotKey(hwnd,1,MOD_NOREPEAT,VK_F8);RegisterHotKey(hwnd,2,MOD_NOREPEAT,VK_F9);RegisterHotKey(hwnd,3,MOD_NOREPEAT,VK_F10);
        SetTimer(hwnd,1,300,nullptr);return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==101) {
            wchar_t path[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=hwnd;
            dialog.lpstrFilter=L"UCI engine (*.exe)\0*.exe\0\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.Flags=OFN_FILEMUSTEXIST;
            if(GetOpenFileNameW(&dialog))SetWindowTextW(engineEdit,path);
        } else if(LOWORD(wp)==102||LOWORD(wp)==103||LOWORD(wp)==104)applyControls();
        else if(LOWORD(wp)==105){std::lock_guard<std::mutex> guard(lock);calibrated=false;hasTL=false;status="Automatic browser coordinates enabled.";}
        else if(LOWORD(wp)==106){std::string fen;{std::lock_guard<std::mutex> guard(lock);if(!latest.data.is_null())fen=latest.data.value("fen",std::string{});}if(!fen.empty()){std::string encoded;for(char c:fen)encoded+=c==' '?"%20":std::string(1,c);auto url=wide("https://lichess.org/analysis/standard/"+encoded);ShellExecuteW(hwnd,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}}
        return 0;
    case WM_DRAWITEM:if(wp==200){drawBoard(*(DRAWITEMSTRUCT*)lp);return TRUE;}break;
    case WM_HOTKEY: {
        if(wp==3){SendMessageW(autoplayBox,BM_SETCHECK,BST_UNCHECKED,0);applyControls();setStatus("Autoplay stopped (F10).");}
        else {POINT p;GetCursorPos(&p);std::lock_guard<std::mutex> guard(lock);
            if(wp==1){calibratedTL=p;hasTL=true;calibrated=false;status="Top-left captured. Point at bottom-right and press F9.";}
            else if(wp==2 && hasTL && fresh(latest) && p.x>calibratedTL.x+100 && p.y>calibratedTL.y+100){calibratedBR=p;calibrated=true;calibrationReference=latest.data;status="Board coordinates calibrated. Recalibrate after moving/resizing the browser.";}
        }return 0;
    }
    case WM_TIMER: {
        std::string text;{std::lock_guard<std::mutex> guard(lock);
            SendMessageW(autoplayBox,BM_SETCHECK,settings.autoplay?BST_CHECKED:BST_UNCHECKED,0);
            text=std::string(settings.autoplay?"AUTOPLAY ARMED":"Autoplay off")+" | "+(fresh(latest)?"Browser connected":"No fresh browser data")+"\r\n"+status+"\r\n\r\n";
            if(!latest.data.is_null())text+=latest.data.value("url",std::string{})+"\r\nFEN: "+latest.data.value("fen",std::string{})+"\r\n\r\n";
            if(fresh(latest) && latest.revision==displayedRevision)text+=analysisText;
        }
        auto value=wide(text);if(controlText(statusEdit)!=value)SetWindowTextW(statusEdit,value.c_str());InvalidateRect(boardView,nullptr,FALSE);return 0;
    }
    case WM_DESTROY:running=false;UnregisterHotKey(hwnd,1);UnregisterHotKey(hwnd,2);UnregisterHotKey(hwnd,3);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\MephistoDesktop17864");
    if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"Mephisto is already running.",L"Mephisto",MB_OK);if(singleton)CloseHandle(singleton);return 0;}
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);baseDirectory=executable;baseDirectory=baseDirectory.substr(0,baseDirectory.find_last_of(L"\\"));
    settingsFile=baseDirectory+L"\\mephisto-settings.json";loadSettings();
    WNDCLASSW cls{};cls.lpfnWndProc=wndProc;cls.hInstance=instance;cls.lpszClassName=L"MephistoDesktop";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&cls);
    HWND hwnd=CreateWindowW(cls.lpszClassName,L"Mephisto Desktop",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,1245,735,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 1;ShowWindow(hwnd,show);
    std::thread server(serverLoop),worker(workerLoop);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    running=false;server.join();worker.join();if(singleton){ReleaseMutex(singleton);CloseHandle(singleton);}return 0;
}
#else
#include "winui-ui.h"
#endif
