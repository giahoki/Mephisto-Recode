#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <microsoft.ui.xaml.window.h>
#include <dwmapi.h>
namespace ui = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
namespace media = winrt::Microsoft::UI::Xaml::Media;
namespace shapes = winrt::Microsoft::UI::Xaml::Shapes;
using winrt::hstring;
std::thread serverThread,workerThread;
int startupShow=SW_SHOW;

struct DesktopApp : ui::ApplicationT<DesktopApp,ui::Markup::IXamlMetadataProvider> {
    ui::Window window{nullptr};
    ui::FrameworkElement root{nullptr};
    ui::DispatcherTimer timer{nullptr};
    ui::XamlTypeInfo::XamlControlsXamlMetaDataProvider provider;
    std::vector<controls::Border> squares;
    std::vector<controls::TextBlock> glyphs;
    std::string boardSignature;
    struct Model {const wchar_t* name;const wchar_t* file;};
    const std::vector<Model> models{{L"Stockfish 17 · NNUE",L"engines\\stockfish-17\\stockfish.exe"},{L"Stockfish 16 · NNUE",L"engines\\stockfish-16\\stockfish.exe"},
        {L"Fairy-Stockfish 14 · варианты",L"stockfish.exe"},{L"Stockfish 11 HCE",L"engines\\stockfish-11\\stockfish.exe"},{L"Stockfish 6",L"engines\\stockfish-6\\stockfish.exe"},{L"Leela Chess Zero",L"engines\\lc0\\lc0.exe"},{L"Другой UCI-движок",L""}};
    bool syncing=false;
    static inline WNDPROC originalProc=nullptr;
    static inline DesktopApp* active=nullptr;
    template<typename T> T get(const wchar_t* name){return root.FindName(name).as<T>();}
    static media::SolidColorBrush brush(unsigned rgb) {
        return media::SolidColorBrush(winrt::Windows::UI::Color{255,(uint8_t)(rgb>>16),(uint8_t)(rgb>>8),(uint8_t)rgb});
    }
    void toggleAutoplay(bool armed) {
        {std::lock_guard<std::mutex> guard(lock);if(settings.autoplay!=armed)armRevision++;settings.autoplay=armed;settingsRevision++;}
        syncing=true;get<controls::ToggleSwitch>(L"Autoplay").IsOn(armed);syncing=false;
    }
    static LRESULT CALLBACK nativeProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
        if(message==WM_HOTKEY && active){
            if(wp==3){bool armed;{std::lock_guard<std::mutex> guard(lock);armed=settings.autoplay;}active->toggleAutoplay(!armed);setStatus(armed?"Автоплей выключен (F10).":"Автоплей включён (F10).");}
            return 0;
        }
        if(message==WM_GETMINMAXINFO){auto info=(MINMAXINFO*)lp;info->ptMinTrackSize={1100,720};}
        return CallWindowProcW(originalProc,hwnd,message,wp,lp);
    }
    void fillSettings(const Settings* supplied=nullptr) {
        syncing=true;
        Settings cfg;{std::lock_guard<std::mutex> guard(lock);cfg=settings;}
        if(supplied)cfg=*supplied;
        get<controls::TextBox>(L"EnginePath").Text(cfg.engine);
        get<controls::TextBox>(L"StartFen").Text(winrt::to_hstring(cfg.startFen));
        const wchar_t* names[]={L"ComputeTime",L"Threads",L"Hash",L"Lines",L"Skill",L"ThinkDelay",L"MoveTime",L"ThinkVariance",L"MoveVariance"};
        int values[]={cfg.time,cfg.threads,cfg.hash,cfg.lines,cfg.skill,cfg.delay,cfg.moveTime,cfg.thinkVariance,cfg.moveVariance};
        for(int i=0;i<9;i++)get<controls::Slider>(names[i]).Value(values[i]);
        const wchar_t* toggles[]={L"PuzzleMode",L"Evaluation",L"Threat",L"Brain",L"Coordinates"};
        bool states[]={cfg.puzzles,cfg.evaluation,cfg.threat,cfg.brain,cfg.coordinates};
        for(int i=0;i<5;i++)get<controls::ToggleSwitch>(toggles[i]).IsOn(states[i]);
        get<controls::ComboBox>(L"Side").SelectedIndex(cfg.side);get<controls::ComboBox>(L"BoardTheme").SelectedIndex(cfg.theme);
        get<controls::ToggleSwitch>(L"Legit").IsOn(cfg.legit);
        auto modelBox=get<controls::ComboBox>(L"EngineModel");modelBox.Items().Clear();int selected=(int)models.size()-1;
        for(size_t i=0;i<models.size();i++){
            std::wstring path=baseDirectory+L"\\"+models[i].file;std::wstring caption=models[i].name;
            if(i<models.size()-1 && GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)caption+=L" · выбрать EXE";
            if(cfg.engine==path)selected=(int)i;modelBox.Items().Append(winrt::box_value(caption));
        }modelBox.SelectedIndex(selected);
        auto box=get<controls::ComboBox>(L"Variant");box.Items().Clear();for(const auto& variant:variants)box.Items().Append(winrt::box_value(winrt::to_hstring(variant)));
        box.SelectedIndex((int)(std::find(variants.begin(),variants.end(),cfg.variant)-variants.begin()));
        syncing=false;
    }
    bool browseEngine(){wchar_t path[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=windowHandle;dialog.lpstrFilter=L"UCI engine (*.exe)\0*.exe\0\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.Flags=OFN_FILEMUSTEXIST;if(!GetOpenFileNameW(&dialog))return false;get<controls::TextBox>(L"EnginePath").Text(path);return true;}
    int value(const wchar_t* name,int low,int high,int fallback){double n=get<controls::Slider>(name).Value();return std::isfinite(n)?std::clamp((int)n,low,high):fallback;}
    void apply() {
        Settings cfg;{std::lock_guard<std::mutex> guard(lock);cfg=settings;}
        cfg.engine=std::wstring(get<controls::TextBox>(L"EnginePath").Text());cfg.startFen=winrt::to_string(get<controls::TextBox>(L"StartFen").Text());
        cfg.time=value(L"ComputeTime",50,30000,1000);cfg.threads=value(L"Threads",1,128,2);cfg.hash=value(L"Hash",16,8192,128);
        cfg.lines=value(L"Lines",1,10,2);cfg.skill=value(L"Skill",0,20,20);cfg.delay=value(L"ThinkDelay",0,10000,250);
        cfg.moveTime=value(L"MoveTime",40,2000,100);cfg.thinkVariance=value(L"ThinkVariance",0,5000,0);cfg.moveVariance=value(L"MoveVariance",0,1000,0);
        cfg.side=std::clamp(get<controls::ComboBox>(L"Side").SelectedIndex(),0,2);cfg.theme=std::clamp(get<controls::ComboBox>(L"BoardTheme").SelectedIndex(),0,2);
        cfg.variant=variants[std::clamp(get<controls::ComboBox>(L"Variant").SelectedIndex(),0,(int)variants.size()-1)];
        cfg.puzzles=get<controls::ToggleSwitch>(L"PuzzleMode").IsOn();cfg.evaluation=get<controls::ToggleSwitch>(L"Evaluation").IsOn();
        cfg.legit=get<controls::ToggleSwitch>(L"Legit").IsOn();
        cfg.threat=get<controls::ToggleSwitch>(L"Threat").IsOn();cfg.brain=get<controls::ToggleSwitch>(L"Brain").IsOn();cfg.coordinates=get<controls::ToggleSwitch>(L"Coordinates").IsOn();
        {std::lock_guard<std::mutex> guard(lock);cfg.autoplay=settings.autoplay;settings=cfg;settingsRevision++;}
        saveSettings();boardSignature.clear();get<controls::TextBlock>(L"Saved").Text(L"Настройки сохранены");
    }
    void createBoard() {
        auto board=get<controls::Grid>(L"BoardGrid");
        for(int i=0;i<8;i++){controls::RowDefinition row;row.Height(ui::GridLength{1,ui::GridUnitType::Star});board.RowDefinitions().Append(row);controls::ColumnDefinition col;col.Width(ui::GridLength{1,ui::GridUnitType::Star});board.ColumnDefinitions().Append(col);}
        for(int r=0;r<8;r++)for(int c=0;c<8;c++){
            controls::Border square;controls::TextBlock glyph;glyph.FontFamily(media::FontFamily(L"Segoe UI Symbol"));glyph.FontSize(49);
            glyph.HorizontalAlignment(ui::HorizontalAlignment::Center);glyph.VerticalAlignment(ui::VerticalAlignment::Center);
            square.Child(glyph);controls::Grid::SetRow(square,r);controls::Grid::SetColumn(square,c);board.Children().Append(square);squares.push_back(square);glyphs.push_back(glyph);
        }
    }
    void board(const State& s,const Settings& cfg,const std::string& best,const std::string& threat,const std::vector<std::string>& arrows) {
        std::string placement=s.data.is_null()?"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR":s.data.value("placement",std::string{});
        bool black=!s.data.is_null() && s.data["orientation"]=="black";
        auto signature=placement+std::to_string(cfg.theme)+std::to_string(black)+std::to_string(cfg.coordinates)+best+threat;for(const auto& move:arrows)signature+=move;
        if(signature==boardSignature)return;boardSignature=signature;
        unsigned lights[]={0xE8D9C2,0xE5E9D9,0xD6DEEA},darks[]={0xA78162,0x819A78,0x778BA8};
        for(int i=0;i<64;i++){squares[i].Background(brush(((i/8+i%8)%2)?darks[cfg.theme]:lights[cfg.theme]));glyphs[i].Text(L"");}
        int file=0,rank=7;std::string types="KQRBNPkqrbnp";std::wstring symbols=L"\u2654\u2655\u2656\u2657\u2658\u2659\u265a\u265b\u265c\u265d\u265e\u265f";
        for(char p:placement){if(p=='/'){rank--;file=0;}else if(p>='1'&&p<='8')file+=p-'0';else{
            auto at=types.find(p);if(at!=std::string::npos&&file<8&&rank>=0&&rank<8){int index=(black?rank:7-rank)*8+(black?7-file:file);glyphs[index].Text(hstring(std::wstring(1,symbols[at])));glyphs[index].Foreground(brush(0x15202B));}file++;}}
        auto canvas=get<controls::Canvas>(L"BoardArrows");canvas.Children().Clear();
        auto arrow=[&](const std::string& move,unsigned color){
            if(move.size()<4||move[0]<'a'||move[0]>'h'||move[1]<'1'||move[1]>'8'||move[2]<'a'||move[2]>'h'||move[3]<'1'||move[3]>'8')return;
            auto center=[&](int f,int r){return winrt::Windows::Foundation::Point{(float)((black?7-f:f)*60+30),(float)((black?r:7-r)*60+30)};};
            auto a=center(move[0]-'a',move[1]-'1'),b=center(move[2]-'a',move[3]-'1');
            shapes::Line line;line.X1(a.X);line.Y1(a.Y);line.X2(b.X);line.Y2(b.Y);line.Stroke(brush(color));line.StrokeThickness(5);line.Opacity(0.8);canvas.Children().Append(line);
            double angle=std::atan2(b.Y-a.Y,b.X-a.X);shapes::Polygon head;head.Fill(brush(color));head.Opacity(0.9);head.Points().Append(b);
            for(double d:{-0.5,0.5})head.Points().Append({b.X-(float)(19*std::cos(angle+d)),b.Y-(float)(19*std::sin(angle+d))});canvas.Children().Append(head);
        };unsigned colors[]={0x346BE0,0x329877,0xB589CF};for(size_t i=0;i<arrows.size()&&i<3;i++)arrow(arrows[i],colors[i]);
        auto coords=get<controls::Canvas>(L"BoardCoordinates");coords.Children().Clear();
        if(cfg.coordinates)for(int i=0;i<8;i++){
            controls::TextBlock f,r;f.Text(hstring(std::wstring(1,(wchar_t)(black?'h'-i:'a'+i))));r.Text(hstring(std::wstring(1,(wchar_t)(black?'1'+i:'8'-i))));f.FontSize(12);r.FontSize(12);f.Opacity(0.6);r.Opacity(0.6);
            controls::Canvas::SetLeft(f,46+i*60);controls::Canvas::SetTop(f,501);controls::Canvas::SetLeft(r,3);controls::Canvas::SetTop(r,42+i*60);coords.Children().Append(f);coords.Children().Append(r);
        }
    }
    void refresh() {
        State s;Settings cfg;std::string message,analysis,best,threat;std::vector<std::string> arrows;
        {std::lock_guard<std::mutex> guard(lock);s=latest;cfg=settings;message=status;if(fresh(s)&&s.revision==displayedRevision){analysis=analysisText;best=displayedBest;threat=displayedThreat;arrows=displayedMoves;}}
        syncing=true;get<controls::ToggleSwitch>(L"Autoplay").IsOn(cfg.autoplay);syncing=false;
        get<controls::TextBlock>(L"Connection").Text(fresh(s)?L"●  Браузер подключён":L"○  Ожидание браузера");
        auto bar=get<controls::InfoBar>(L"StatusBar");bar.Title(cfg.autoplay?L"Автоплей включён":L"Состояние");bar.Message(winrt::to_hstring(message));
        bar.Severity(message.rfind("Error:",0)==0?controls::InfoBarSeverity::Error:controls::InfoBarSeverity::Informational);
        get<controls::TextBlock>(L"Analysis").Text(analysis.empty()?L"Ожидание актуального анализа…":winrt::to_hstring(analysis));
        get<controls::TextBlock>(L"BestMove").Text(best.empty()?L"—":winrt::to_hstring(best));
        if(!s.data.is_null()){
            auto fen=s.data.value("fen",std::string{});get<controls::TextBlock>(L"Fen").Text(winrt::to_hstring(fen));get<controls::TextBlock>(L"PageUrl").Text(winrt::to_hstring(s.data.value("url",std::string{})));
            get<controls::TextBlock>(L"Turn").Text(fen.find(" w ")!=std::string::npos?L"Ход белых":fen.find(" b ")!=std::string::npos?L"Ход чёрных":L"Ход неизвестен");
        }board(s,cfg,best,threat,arrows);
    }
    void OnLaunched(ui::LaunchActivatedEventArgs const&) {
        Resources().MergedDictionaries().Append(controls::XamlControlsResources());
        std::ifstream file(baseDirectory+L"\\ui.xaml",std::ios::binary);std::string xaml((std::istreambuf_iterator<char>(file)),{});
        root=ui::Markup::XamlReader::Load(winrt::to_hstring(xaml)).as<ui::FrameworkElement>();
        window=ui::Window();window.Title(serverPort==17864?L"Mephisto Desktop":L"Mephisto Desktop - Test");window.Content(root);window.ExtendsContentIntoTitleBar(true);window.SetTitleBar(get<ui::UIElement>(L"TitleBar"));
        try{window.SystemBackdrop(media::MicaBackdrop());}catch(...){}
        auto interop=window.as<IWindowNative>();winrt::check_hresult(interop->get_WindowHandle(&windowHandle));
        active=this;originalProc=(WNDPROC)SetWindowLongPtrW(windowHandle,GWLP_WNDPROC,(LONG_PTR)nativeProc);
        SetWindowPos(windowHandle,nullptr,0,0,1420,920,SWP_NOMOVE|SWP_NOZORDER);
        RegisterHotKey(windowHandle,3,MOD_NOREPEAT,VK_F10);
        fillSettings();createBoard();
        get<controls::NavigationView>(L"Navigation").SelectionChanged([this](auto const&,controls::NavigationViewSelectionChangedEventArgs const& args){
            auto item=args.SelectedItem().try_as<controls::NavigationViewItem>();bool overview=!item||winrt::unbox_value<hstring>(item.Tag())==L"overview";
            get<ui::UIElement>(L"Overview").Visibility(overview?ui::Visibility::Visible:ui::Visibility::Collapsed);get<ui::UIElement>(L"SettingsView").Visibility(overview?ui::Visibility::Collapsed:ui::Visibility::Visible);
        });
        get<controls::ToggleSwitch>(L"Autoplay").Toggled([this](auto const& sender,auto const&){if(!syncing)toggleAutoplay(sender.as<controls::ToggleSwitch>().IsOn());});
        get<controls::Button>(L"Stop").Click([this](auto const&,auto const&){toggleAutoplay(false);setStatus("Autoplay stopped.");});
        get<controls::Button>(L"Apply").Click([this](auto const&,auto const&){apply();});
        get<controls::Button>(L"Browse").Click([this](auto const&,auto const&){if(browseEngine()){syncing=true;get<controls::ComboBox>(L"EngineModel").SelectedIndex((int)models.size()-1);syncing=false;}});
        get<controls::ComboBox>(L"EngineModel").SelectionChanged([this](auto const&,auto const&){
            if(syncing)return;int index=get<controls::ComboBox>(L"EngineModel").SelectedIndex();if(index<0)return;
            std::wstring path=baseDirectory+L"\\"+models[index].file;
            if(index<(int)models.size()-1 && GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)get<controls::TextBox>(L"EnginePath").Text(path);
            else browseEngine();
        });
        get<controls::Button>(L"Recommended").Click([this](auto const&,auto const&){Settings recommended;recommended.engine=baseDirectory+L"\\engines\\stockfish-17\\stockfish.exe";if(GetFileAttributesW(recommended.engine.c_str())==INVALID_FILE_ATTRIBUTES)recommended.engine=baseDirectory+L"\\stockfish.exe";fillSettings(&recommended);get<controls::TextBlock>(L"Saved").Text(L"Оптимальные значения выбраны. Нажмите «Сохранить настройки».");});
        const wchar_t* sliderNames[]={L"ComputeTime",L"Threads",L"Hash",L"Lines",L"Skill",L"ThinkDelay",L"MoveTime",L"ThinkVariance",L"MoveVariance"};
        const wchar_t* sliderCaptions[]={L"Анализ, мс",L"Потоки",L"Память, МБ",L"Линии анализа",L"Сила",L"Задержка хода, мс",L"Между кликами, мс",L"Разброс ожидания, мс",L"Разброс кликов, мс"};
        for(int i=0;i<9;i++){auto slider=get<controls::Slider>(sliderNames[i]);std::wstring caption=sliderCaptions[i];
            slider.Header(winrt::box_value(caption+L": "+std::to_wstring((int)slider.Value())));
            slider.ValueChanged([caption](auto const& sender,auto const& args){sender.as<controls::Slider>().Header(winrt::box_value(caption+L": "+std::to_wstring((int)args.NewValue())));});
        }
        get<controls::Button>(L"ResetCalibration").Click([](auto const&,auto const&){std::lock_guard<std::mutex> guard(lock);calibrated=false;hasTL=false;armRevision++;status="Координаты будут автоматически перепроверены перед следующим кликом.";});
        get<controls::ComboBox>(L"AppTheme").SelectionChanged([this](auto const&,auto const&){root.RequestedTheme(get<controls::ComboBox>(L"AppTheme").SelectedIndex()==0?ui::ElementTheme::Dark:ui::ElementTheme::Light);});
        get<controls::Button>(L"OpenAnalysis").Click([](auto const&,auto const&){std::string fen,variant;{std::lock_guard<std::mutex> guard(lock);if(!latest.data.is_null())fen=latest.data.value("fen",std::string{});variant=settings.variant;}if(!fen.empty()){std::string encoded;for(char c:fen)encoded+=c==' '?"%20":std::string(1,c);std::map<std::string,std::string> ids{{"chess","standard"},{"fischerandom","chess960"},{"3check","threeCheck"},{"kingofthehill","kingOfTheHill"},{"racingkings","racingKings"}};if(ids.count(variant))variant=ids[variant];auto url=wide("https://lichess.org/analysis/"+variant+"/"+encoded);ShellExecuteW(windowHandle,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}});
        timer=ui::DispatcherTimer();timer.Interval(300ms);timer.Tick([this](auto const&,auto const&){refresh();});timer.Start();refresh();
        window.Closed([this](auto const&,auto const&){timer.Stop();running=false;for(int i=1;i<=3;i++)UnregisterHotKey(windowHandle,i);});
        window.Activate();
        if(startupShow==SW_HIDE)ShowWindow(windowHandle,SW_HIDE);
        serverThread=std::thread(serverLoop);workerThread=std::thread(workerLoop);
    }
    ui::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type){return provider.GetXamlType(type);}
    ui::Markup::IXamlType GetXamlType(hstring const& name){return provider.GetXamlType(name);}
    winrt::com_array<ui::Markup::XmlnsDefinition> GetXmlnsDefinitions(){return provider.GetXmlnsDefinitions();}
};

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int show) {
    startupShow=show;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t portText[16]{};if(GetEnvironmentVariableW(L"MEPHISTO_PORT",portText,16)){try{serverPort=std::clamp(std::stoi(portText),1024,65535);}catch(...){}}
    auto mutexName=L"Local\\MephistoDesktop"+std::to_wstring(serverPort);
    HANDLE singleton=CreateMutexW(nullptr,TRUE,mutexName.c_str());
    if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"Mephisto is already running.",L"Mephisto",MB_OK);if(singleton)CloseHandle(singleton);return 0;}
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);baseDirectory=executable;baseDirectory=baseDirectory.substr(0,baseDirectory.find_last_of(L"\\"));settingsFile=baseDirectory+L"\\mephisto-settings.json";loadSettings();
    int result=0;
    try{winrt::init_apartment(winrt::apartment_type::single_threaded);ui::Application::Start([](auto&&){winrt::make<DesktopApp>();});}
    catch(const winrt::hresult_error& e){std::ofstream(baseDirectory+L"\\startup-error.txt")<<winrt::to_string(e.message());MessageBoxW(nullptr,e.message().c_str(),L"Mephisto — WinUI initialization",MB_ICONERROR);result=1;}
    running=false;if(serverThread.joinable())serverThread.join();if(workerThread.joinable())workerThread.join();if(singleton){ReleaseMutex(singleton);CloseHandle(singleton);}return result;
}
