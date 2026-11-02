# Pokémon Fulldex 日本語ROM対応：引き継ぎメモ

対象リポジトリ: `ak2azure/pokemon-fulldex-jp`（`mamesj/pokemon-fulldex` のフォーク）
目標: 本家が対応している全ゲームを日本語版ROMでも遊べるようにする（Switch版。Android版は作らない）

> このファイルは新しいセッションへの引き継ぎ用。`CLAUDE.md` から読み込まれる。作業が進んだらここを更新してコミットすること。

## 作業のしかた（2026-11-02 時点）

- **Cowork セッション（ユーザーPCとリンク）では git push できない。** リポジトリがセッションの許可リストに無いため、push も GitHub API も 403。Cowork にはリポジトリを選ぶ画面が無い。
  - 代わりに、ユーザーPCのクローン `C:\Users\user\Documents\GitHub\pokemon-fulldex-jp`（GitHub Desktop）にフォルダアクセスをもらい、`device_commit_files` で書き込む → GitHub Desktop で Commit/Push（computer use で操作するか、ユーザーに依頼）。
  - もう一つの方法: アプリ内ブラウザ（GitHubにサインイン済み）の upload ページで、JS から `#upload-manifest-files-input` に File を入れてコミット。Actions の手動実行もブラウザから。
  - 読むだけなら公開リポジトリなので `git clone https://github.com/ak2azure/pokemon-fulldex-jp.git` でできる。
- **Claude Code デスクトップ（ユーザーPC、2026-11-02〜）: git と gh が入り、gh はログイン済み。** ここから commit/push、`gh workflow run`、`gh run download` ができ、ROM も直接読める。
- **Claude Code（クラウド）でリポジトリを選んで始めれば直接 push できる。** ただしユーザーPCの ROM には届かない見込みなので、ROM が必要な作業は Cowork 側で。
- 作業環境（ROMの取り込み、SS の gen/、逆コンパイルのシンボル、PS5 SDK）はセッションをまたいで残らない。必要なら再取得: ROM はユーザーPCから device_stage_files（大きいものはZIPで）。gen/ は北米版SS ROMから `tools/recomp.py`、PS5 SDK は pacbrew-repo のリリース `ps5-payload-dev.tar.gz`。
- Switch用ファイルは GitHub Actions で作る（作業環境では devkitPro がダウンロードできない）。

## ここまでの状況

- **日本語版ブラック（IRBJ）: 完了。** Switch実機で動作確認済み（fpsが少し落ちる場面あり。英語版と共通の可能性が高い）。
- **日本語版ホワイト（IRAJ）: 完了。** Switch実機で動作確認済み（2026-11-02）。
  - main に反映済み（コミット 828f585, 9c1352b, 569b862）。Actions「Switch Black and White」#2 成功（成果物 pokemon-bw-switch）。
  - セーブは `white-jp.sav`。
- ワークフロー `.github/workflows/switch-bw.yml`（「Switch Black and White」）: 手動実行、4分弱、成果物 `pokemon-bw-switch`。
  - 中の `pokemon-black-white-wide-switch.zip` を展開して `switch` フォルダをSDカードのルートへ。ROMは `/switch/pokemon-black-wide/`。

## 全ゲーム版（1つのnro）

- 本家の `tools/switch/build_game.sh all`（起動時のゲーム一覧＋設定画面、pokemon-ds-wide.nro）を、SoulSilver(US) ROMなしでも作れるようにした（ROM省略時は tools/bw2/stubgen を使い、SS/HGは一覧に出ない）。`tools/switch/build.sh` はJITビルドなら gen/ を要求しないよう修正。
- ワークフロー `.github/workflows/switch-all.yml`（「Switch all games」）。成果物 `pokemon-ds-wide-all-switch`（中に pokemon-ds-wide-all-switch.zip）。
- SDカード: `switch/pokemon-ds-wide/pokemon-ds-wide.nro`、ROMは `switch/pokemon-ds-wide/roms/`（名前自由）。

## UIの日本語化（2026-10-31）

- `frontend/i18n.h`（Tr: 描画直前に英語→日本語、合成文字列のルール）、`frontend/i18n_ja.h`（訳語表）、`frontend/jpfont.h`（12ドット、`tools/i18n/make_font.py` で efont b12.bdf から生成。BDFは u8g2 リポジトリの tools/font/bdf/b12.bdf を git sparse-checkout で取得）。
- `ui.h` の Canvas::Text/TextWidth が UTF-8 対応＋Tr。日本語は JpScale=round(7*scale/12) 倍。設定ページは日本語時 ts=s（720pで3）。説明文は WrapPx で翻訳後に折り返し、Text(..., false) で再翻訳しない。
- 設定「GAME LIST > LANGUAGE」（list_language: ja 既定 / en）。main.cpp の設定保存にも追加。
- Actions「Switch all games」#3 成功、実機確認済み（完了）。訳語を足すときは i18n_ja.h に追加し make_font.py を再実行（jpfont.h 再生成）。

## 日本語版プラチナ（CPUJ）（2026-10-31）

- `tools/match_table.py`（英語版の関数表＋英語版ROM＋日本語版ROM → 日本語版の関数表。サイズは次の関数まで）で `tools/platinum/CPUJ.txt` を作成。34,659中33,420一致、監視関数はすべて一致。
- launcher.h（CPUJ、platinum-jp.sav）、platinum.h（CPUJも on）、build_game.sh（romfsにCPUJ.txt）。Actions「Switch all games」#4 成功、実機確認済み（完了）。

## 日本語版ブラック2（IREJ）

- 完了（実機確認済み）。match_table.py で照合（42,691中41,190、監視関数は全部、うち4つは類似度）→ `tools/bw2/jp_black2_shifts.txt`（ずれ表）＋ `tools/bw2/funcmap.py --jp-black2`。
- bw2map.h IREJ: 0x021c9768, 0x0208eb48, 0x1a3800, 29。exclusives/launcher/i18n対応。Actions #5 成功（ログでIREJ表の生成を確認）。
- ホワイト2日本語版はROMなし。

## ソウルシルバー/ハートゴールド日本語版（IPGJ/IPKJ）

- 全ゲーム版はJIT＋表のみ（tables_only）で動くので、HeartGold US と同じ仕組み（tools/soulsilver/*.json の shifts、runtime の Remap）で対応。基準は北米版SS(IPGE)。
- `tools/soulsilver/match_japanese.py`（チャンク＋layoutのnamed関数単位で照合、類似度0.8で補完）→ soulsilver-jp.json / heartgold-jp.json（各約636区間）。約38,300中708未発見（ボルトロスのスロット等、日本版にない物）。監視する関数（apps/focus/hook）はボルトロス以外すべて一致。
- 北米版SSの表 `tools/soulsilver/tables/`（rc_tables.cpp, apps_table.cpp：アドレスのみ）をリポジトリに置き、build_game.sh all（ROMなし）が tables_only.py でそこから作る → 北米版SS/HGも全ゲーム版で遊べる。
- フロント（main.cpp soulSilverRom、Playable、heartSilver、launcher、pc_launcher、i18n）対応済み。Actions「Switch all games」#6 成功、ログで4本（SS/HG 北米・日本）の表を確認。
- 不具合修正（11/1）: 実機で日本語版HGの通常マップがワイドにならなかった。原因は日本語版のオーバーレイの読み込み先アドレスが北米版と違う（field: -0xAC0 など）のに、ずれを「オーバーレイ内のずれ」としか扱っていなかったこと。json の各セクション先頭に [0,0,move] を追加し、runtime/recomp_rt.cpp の Remap と rc_portable_load（セクションbase）で move を足すようにした。FieldMap_Main が正しい位置になることを確認。#8 成功、実機確認済み（完了）。

## PS5 版（2026-11-02、テスト版・実機未確認）

- jailbreak した PS5 の elfldr（ポート9021）に送る ELF ペイロード。PC 全ゲーム版（`pc_launcher.cpp`、SSR_PC_ALL_GAMES）をそのまま使い、ゲームリスト・ワイド表示・日本語メニューも入る。
- ツールチェーン: ps5-payload-sdk ＋ pacbrew-repo のリリース `ps5-payload-dev.tar.gz`（SDL2 などのライブラリ込み、/ に展開すると /opt/ps5-payload-sdk）。clang-18 / lld-18。
- ビルド: `tools/ps5/build.sh`（ROM不要。SS/HG の表は tools/soulsilver/tables、BW 系は逆コンパイルのシンボルから）。ワークフロー `.github/workflows/ps5.yml`（成果物 `pokemon-fulldex-ps5`: homebrew/PokemonFulldex/、README.txt、pokemon-ds-wide/）。gh CLI でログイン済み（ak2azure）: `gh workflow run ps5.yml`、`gh run download`。
- 実行時: フォルダーは `/data/pokemon-ds-wide`（data/funcs、roms、saves、settings.cfg、game.log）。SDL の PS5 ドライバー（VideoOut のフレームバッファ、scePad、AudioOut）。描画は SDL の software レンダラー、JIT なし（インタプリタ）、GPU3D なし。
- コード: CMake の `PS5`（toolchain が設定）→ `SSR_PS5` 定義、出力名 pokemon-fulldex.elf。ffsystem.c の localtime_r 対策に `-include time.h`。pc_launcher.cpp: PS5 のフォルダー、--jit を渡さない、Finish は何もしない（再起動できないので終了）。main.cpp: software レンダラー、常にフルスクリーン。
- 実機（FW 11.0）で elfldr に直接送ったら画面が出なかった（11/2）。game.log は `[launcher] no window: Couldn't find matching render driver`。
  - PS5 の SDL2（pacbrew の ps5-payload-sdl2、Mesa パッチ入り）の render driver は software だけ。software は画面バッファを作るときに VideoOut を開く（sceVideoOutOpen、直接メモリ）。そこで失敗しているが、SDL が本当のエラーを上書きする。
  - SDL アプリ（offact など）は `/data/homebrew/<名前>/eboot.elf` に置き、websrv のホームブリュー一覧から起動する前提。elfldr に直接送ると画面を持てないと判断。
  - 対応: `frontend/ps5.h`（画面の隅の通知 sceKernelSendNotificationRequest、SDL_GetWindowSurface を先に呼んで本当のエラーを出す、FRAMEBUFFER_ACCELERATION=0、終了時 sceSystemServiceLoadExec("exit")）。配布物は `homebrew/PokemonFulldex/`（eboot.elf、homebrew.js、sce_sys/icon0.png は tools/ps5/make_icon.py で生成）＋ `pokemon-ds-wide/`。
  - websrv（Homebrew Launcher v0.34）から起動すると VideoOut は開けた。ただし SDL 2.28 以降は、ウィンドウに画面バッファがあるとレンダラーを作らない（"Surface already associated with window"）。そのため PrepareWindow は確認後に SDL_DestroyWindowSurface する。
  - **ゲーム一覧の表示まで実機で確認（11/2）。** 起動後、ランチャーのダイアログを○で閉じると見える。
  - 速度（ブラック、1080p）: 最初は30fps・ゲーム速度半分。PS5 の SDL の表示はフリップを待つので、パイプライン（SSR_PIPELINE）をオンにした。合成は GPU合成（software renderer で 1080p を1コア処理、13〜34ms）をやめて CPU合成（merged.h、複数スレッド、画面サイズのまま）にし、2〜3.5ms になった。残りはエミュレーション（インタプリタ）16.4〜17ms。
  - `-march=znver2` は効果なし（clang の PS5 ターゲットは最初から znver2）。SSR_EMU_PROF は GPU3D::Run ごとに時刻を読むので切った。効果は未確認。
  - JIT（11/2、実機未確認）: PS5 SDK の libc は mmap/mprotect の PROT_EXEC を kernel_mprotect で通す（sdk/libc/mman.c）。SDK の起動処理は jitshm の権限も付ける。melonDS の x64 JIT は実行用のメモリさえあれば動くので、SSR_JIT=ON にした。fastmem（shm_open、8GB の予約、SIGSEGV）は使わない。`ARMJIT_Memory.cpp` の `__PROSPERO__` で DS のメモリを普通のバッファにした（fastmem なしでは Mappings が空のままなので、map/unmap/protect は呼ばれない）。起動時に `Ps5::CanRunJit()`（RWX のページで ret を実行）が通れば --jit。SSR_PS5_INTERP=1 でインタプリタ。
  - **JIT は実機（FW 11.0）で動いた（11/2）。** ブラックでエミュレーションは 16.5ms から約6ms になり、ワイド表示の3Dは 1080 本に戻った。早送りも動く。まだ 17.5ms を超えるフレームがときどきある（worst 27〜43ms、present に揺れがある）。
  - GPU（4K）（11/2）: tools/ps5/build.sh で SDL を自分でビルドする（ps5-payload-dev/SDL＋pacbrew のパッチ＋SDL_VIDEO_RENDER_OGL_ES2）。Mesa（libEGL.so.1、libGLESv2.so.2、libgallium-26.2.0.so、libvulkan.so.1）を eboot.elf と同じフォルダーに入れる。**SDK のローダーは LD_LIBRARY_PATH を「:」で区切れない**ので、フォルダーは1つだけ渡す。実機で `OpenGL ES 3.2 Mesa 26.2.0, zink Vulkan 1.4 (PlayStation 5 GPU (RADV NAVI21))`、3840x2160、SDL renderer opengles2 を確認。合成は 0.1〜0.6ms。Mesa の出力は常に 3840x2160 で、モニターの解像度に関係ない。
  - ただし、ワイド表示の3Dは CPU が描くので、WIDESCREEN RES が 2160 だと間に合わない（wide 待ち 17ms）。PS5 では LOWER RES WHEN BUSY を既定でオンにした。3D も GPU で描く（WORLD ON GPU、GL 4.3 が必要。zink は GL 4.6）のが次の段階。
  - WORLD ON GPU（11/2、実機未確認）: PS5 も SSR_GPU3D=ON。GPU3D_WideGpu_GL.cpp が専用スレッドで desktop GL 4.3 core のコンテキストを作る。PS5 では glcontext.cpp が libEGL.so.1 を RTLD_GLOBAL で dlopen して（SDL より先。後から SDL が読み込むときも同じものを共有する）関数を dlsym で取る。EGL はリンクしない（起動時のローダーは LD_LIBRARY_PATH を知らないため）。libGL.so.1 も配布物に入れた。失敗して落ちたら、次回の起動で自動的にオフになる（WideGpuMarker）。
  - **WORLD ON GPU は実機で動いた（11/2）。** フィールドは 2160本・4K 出力で 60fps（wide の待ち 0ms）。ポーズメニューは 4K で CPU が描いて 24ms かかっていたので、PS5 で 4K のときはオーバーレイを半分のサイズで描いて GPU で2倍に拡大するようにした（scene 5ms、遅れたフレームは 300中6）。メニューの上下は押し続けるとリピートする（共通部分の変更）。
- HG 日本語版のバトル（11/2、実機で直ったことを確認）: 横長ボタンの切り分け（gen4BattleInput の Cut）は英語版の文字幅で決め打ちだったので、画面の列を見て文字の範囲を広げるようにした。技の欄が使えるかの判定は1点から15点にした（日本語版の3つ目・4つ目の技に行けなかった）。本家 issue #39「4th move is not selectable」も同じ問題かもしれない。
- 室内の端が伸びる（11/2、直した）: merged.h の MergedFillWideHoles が、ワイド部分の何も描かれていない所を DS 画面の端から外へ最後の色で埋めていた（屋外でゲームが遠くの地面を描かない所のため）。室内では部屋の外の黒まで埋めて縞になっていた。DS 画面内で同じ行に画面幅の1/32を超える穴がある行は埋めないようにした。WSL のヘッドレス版で HG 室内が直り、HG 屋外5・プラチナ屋外6・ブラック7場面は1画素も変わらないことを確認。
- BW の晴れのレンズフレア（11/2）: 3Dの四角形1枚（A5I3、ゲームの最後のポリゴン、WEATHER_Draw が描く）で、DS 画面の左上の角を覆う。ワイド表示では世界と一緒に中央へ縮められ、DS 画面の角から光っていた。晴れの天気（WEATHER_CLEAR_Run、bw1.h）の間は WideState::FlareAtLeft を立て、GPU3D_Wide.cpp で「A5I3 の四角形で、左上の角を含み、幅・高さとも画面の1/4以上」のポリゴンを左端基準に縮める。ホワイトの自宅前で、フレアがワイドの角から出て、フレアのないフレームは1画素も変わらないことを確認。調べ方: WSL で一時的にポリゴンを番号の範囲で外して二分探索した。
- BW で画面が動くときのちらつき（ポケモンセンターの窓など、11/2、実機未確認）: 60fps の中間フレームで、横に並んだ別の絵へ切り替わるテクスチャ座標まで中間にしていた。本家 6f041ea（11/1 ホットフィックス）の GPU3D_Wide.cpp の修正だけを移植した（本家の他の変更はまだ取り込んでいない）。ホワイトの屋外31フレーム（中間フレームあり、`--interp60`）では1画素も変わらないことを確認。ヘッドレスは中間フレームが既定でオフなので、比べるときは `--interp60` を付ける。
- BW のポケモンセンター入口の柱の根元のちらつき（11/2、そのままにすると決めた）: 30fps でも起き、縦移動のときだけ出る。柱の根元・扉の下の敷石・窓の下の帯が地面とほぼ同じ奥行きで重なる Zファイティングで、DS 本来の描画（--no-wide）でも同じように起きている（4K だと目立つ）。深度バイアスで止められるが、重なりのルールが全場面で変わるので入れていない。調べ方: WSL でカメラの縦の動きを打ち消して根元を並べて比べた。
- BW の屋上で左上の木が画面端へ伸びる（11/2）: GPU3D_Wide.cpp の ExtendToEdges（遠くの層を画面端まで伸ばす。主に BW2 の展望台や空のため）が、見下ろしの町の上の方の木や地面を「遠くの層」と判定して伸ばしていた（farW の1割以内）。奥行きの比（near/far 3.66）では空のある場面と見分けられないので、BW1 ではオフにした（Wide.ExtendEdges = funcWatchOn && !pt.on && !bw1.on、BW2 はオンのまま）。BW1 の3場面（ホワイト自宅前29、ブラックのポケモンセンター前37、屋上37フレーム）でこの処理が絵を変えたのは屋上の木の16フレームだけで、オフにするとそれが直る。BW1 で空の見える場所（あれば）は未確認。
- 走ると岩の面が中間フレームでずれて地面が見える（11/2、30fps では起きない）: GPU3D_Wide.cpp の Pair が場面の動きを画面上の動き（clip/w）で多数決していたので、カメラに近い岩（画面上で場面の4〜16倍動く）が相手を見つけられず、新しい位置のまま描かれていた。場面の動きを clip 座標（奥行きによらず同じ）で測り、典型的な奥行き（ref、w の中央値）で Pair の単位にした。相手のないポリゴンは、組になったポリゴンの clip の平均の動き（SceneX/Y/W）の半分だけ戻す。ブラックの岩の道でダッシュ（`--interp60`、200フレーム）: 1フレームだけの乱れが 1500画素を超えるフレーム 84→4。ホワイトの歩き・ポケモンセンター前は変化なし、HG の歩きは 45→39。比べ方: scratchpad の flicker.py（前後のフレームを横か縦にずらして合わせ、両隣が一致するのにそのフレームだけ違う画素を数える）。
- PS5 の終了でゲーム一覧に戻る（11/2、実機未確認）: ペイロードは自分を起動し直せないので、終了の後片付け（セーブ、SDL_Quit）が済んだら PcLauncher::Finish → Ps5::Relaunch が websrv（127.0.0.1:8080）に `GET /hbldr?pipe=0&path=<eboot.elf>` を送る。websrv の hbldr_launch は動いているアプリ（自分）を sceSystemServiceKillApp で閉じてから、FAKE00000 を起動して ELF を入れ直す。応答が来たら（失敗）または20秒たったら普通に終わる。websrv なしで起動したときは接続できずに終わる。
- PS5 のホーム画面アプリ（11/2、実機未確認）: ユーザー環境は etaHEN＋kstuff-lite（＋ShadowMountPlus を入れる）。フォルダーは `/data/homebrew/PKFD00001`（タイトル ID PKFD00001）。
  - eboot.bin は ps5-native-app-boilerplate（leolobato、38c68b4、GPL-3）でビルドするネイティブアプリ（tools/ps5/launcher/main.cpp、sceNet で starter.elf を 127.0.0.1:9021 に送って、画面に「STARTING...」か失敗理由を出す）。ボイラープレートのタイトル ID チェックは PPSA だけなので、build.sh で [A-Z]{4} に書き換える。sce_sys は icon0.png と param.json だけ（背景は任意）。
  - starter.elf（tools/ps5/starter）は websrv（7748df9）の src/ps5/hbldr.c・elfldr.c・pt.c をビルド時に取ってきてリンクするペイロード。websrv と同じく sceUserServiceInitialize と authid 0x4801000000000013 のあと、/data/homebrew などから param.json に PKFD00001 がある eboot.elf を探して hbldr_launch（動いているアプリを閉じ、FAKE00000 を起動して eboot.elf に差し替える）。なので本体は websrv から起動したときと同じ権限で動く（JIT、/data、GPU）。
  - 終了（QUIT）で一覧に戻るのも starter.elf を 9021 に送る方式に変えた（starter.elf がなければ websrv の /hbldr）。
  - ネイティブアプリとして本体を作り直す案は、ペイロード SDK の kernel_* に頼る JIT・/data が使えなくなるおそれがあるのでやめた。pkg（fPKG）にするなら、同じフォルダーを PS5-Suite の PKG ビルダーで固める方法がある（未着手）。→ 11/3 に Porpoise の方式でネイティブ化した（下の「PS5 のタイトル版」）。
- PS5 のタイトル版（11/3、**実機で起動し、ゲームも動いた**）: 本体そのものを eboot.bin にしたネイティブタイトル PKFD00002「Pokémon Fulldex Native」。Porpoise（Dolphin、PPSA99764、https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5）と同じ作り。ペイロード版（PKFD00001）は実機で確認できるまで残す。
  - ビルド: `tools/ps5/build_native.sh`（ワークフロー ps5.yml の ps5-native ジョブ、成果物 `pokemon-fulldex-ps5-native` はフォルダー PKFD00002/、PKFD00099/、pokemon-ds-wide/、licenses/）。表は `PS5_TABLES_ONLY=1 tools/ps5/build.sh` で作る。
  - 作り方: CMake は今までどおり SDK の toolchain でコンパイルし、`-DSSR_PS5_NATIVE=ON` で ssrecomp の RULE_LAUNCH_LINK を `tools/ps5/native_link.py` にする。これが CMake のリンクコマンドから .o/.a を拾い、boilerplate（38c68b4）の app_crt.o・ps5-pie.ld（＋unwind 用の境界シンボル、tools/ps5/native/ps5-pie-unwind.ld）でリンクし、boilerplate の変換ツール（ps5-native-tool link）でコンソールのモジュール import にする。署名（self --sign）と sce_module/libc.prx（boilerplate が自分のソースから作る）も boilerplate のもの。
  - Mesa は使えない（タイトルでは dlopen が通らない、と Porpoise の build.sh に記録）。なので画面は SDL のソフトウェア描画（ps5 ビデオドライバーは sceVideoOut を直接使うので、タイトルでも動くはず）、1920x1080、SSR_GPU3D=OFF。Ps5::UseGpu は native では false。GPU に戻すには Mesa（zink＋EGL）を PS5_Mesa の RADV と一緒に静的リンクするか、描画を Vulkan（PS5_Vulkan の RADV、Porpoise と同じ）にする必要がある。
  - PS5_PayloadSDK（mihawk-99 の SDK フォーク、e105cf6）の platform/ 層を自分のスクリプトでコンパイルしてリンク（libps5platform.a）。使っているもの: ヒープ（malloc 系を --wrap、直接メモリの dlmalloc。タイトルの libc のヒープはすぐ尽きる）、スレッド（pthread_create を --wrap。タイトルのスレッドの既定スタックは 64KB しかない）、実行できるメモリ（ps5_exec_allocate: 直接メモリを RW で張ってから sceKernelMprotect で RWX、プログラムの ±2GB 以内）、タイトルで使えない libc 関数（opendir 系は getdents で、*at 系、localtime_r、getaddrinfo など。tools/ps5/native/wraps.c）。
  - native_link.py はリンク後に import の一覧（.imports）を書き、platform 層の libc.h が「タイトルには実体がない」と挙げる関数が残っていたら失敗する（SDK のスタブはそれらも載せているので、リンクは通ってしまう）。libkernel_sys のスタブは渡さない。
  - JIT: ARMJIT_Global.cpp の `__PROSPERO__ && SSR_PS5_NATIVE` で、128MB の静的配列をやめ、初回に ps5_exec_allocate（プログラムの近く）で 4 スライス分を取る。Ps5Native::CanRunJit も同じ方法で ret を実行して確かめる。
  - /data: 起動直後（スレッドを作る前）に seteuid(geteuid())、/data/pokemon-ds-wide に書けなければ etaHEN に頼む（/download0/etahen_jailbreak などに {"PID":n} を .tmp 経由で、だめなら 127.0.0.1:9028/9069 にコマンド5）。最大10回。frontend/ps5_native.h（Porpoise の porpoise_jailbreak.cpp を移植、GPL-3）。届かなければ game.log は /download0 に書き、通知を出す。
  - 終了: QUIT は sceSystemServiceLoadExec("/app0/eboot.bin")（Porpoise の再起動と同じ）、終わるときは LoadExec("exit")。スプラッシュは SDL の ps5 ビデオドライバーが画面を開くときに sceSystemServiceHideSplashScreen を呼ぶ。
  - WSL でも Actions（ブランチ claude/heuristic-villani-21fcd2、run 41486703480、ps5 と ps5-native とも成功）でもビルドが通ることを確認。ユーザーには C:SERSSERDOCUMENTSPOKEMON-FULLDEX-PS5-NATIVE Á�ű�ɖ�Á�Á�Ƹ�Á�Á�ϼ�eboot.bin 約 8MB、import 336 個、タイトルに実体のない関数なし）。リンクで分かったこと: libc++abi の __cxa_thread_atexit_impl はどのモジュールにもないので wraps.c で platform 層へ。thread_local の初期化フック（_ZTHN7melonDS3NDS7CurrentE）が弱い未定義のまま残ると変換ツールが失敗するので、native_link.py が空の関数を作って再リンクする。libunwind 用に __eh_frame_start などをリンカースクリプトで定義。
  - 実機 1 回目（11/3）: PKFD00099 は /data・ROM 一覧・1GB ヒープ・JIT まで OK、SDL_Init の中で止まった（エラーは返らず）。SDL の ps5 ビデオドライバーはデバイスを作るときに sceKeyboardInit（USB キーボード）を呼ぶので、タイトルで読み込めないモジュールへの呼び出しと見て、sceKeyboard*・sceImeDialog* を wraps.c で「キーボードなし」にした。あわせて、落ちたときに game.log / native-probe.log へシグナル・アドレス・スタック上の戻りアドレスを書く（Ps5Native::LogCrashes）、リンクマップ（build-ps5-native-work/*.map）を残す、テスト用タイトルは SDL を部分ごとに起動して記録、を入れた。
  - 実機 2 回目（11/3）: キーボードを外したビルド（04842ef）で、タイルから起動してゲームも普通に動いた（ユーザー確認）。SDL_Init で止まった原因はキーボードだったと見てよい。
  - テスト用タイトル PKFD00099「Fulldex Test」（tools/ps5/native/probe.cpp、11/3 から配布物には入れない。`PS5_PROBE=1 tools/ps5/build_native.sh` で build-ps5-native-work/PKFD00099 に作る）: /data、ROM 一覧、1GB のヒープ、JIT、SDL、画面、音、パッドを順に試し、四角（緑/赤）と /data/pokemon-ds-wide/native-probe.log に出す。
- PS5 タイトル版の Vulkan 化（11/3、PC で確認済み・実機未確認）: ユーザーの依頼で 4K と WORLD ON GPU をタイトル版に戻す。Mesa（GL）はタイトルで読めないので Vulkan（RADV を静的リンク、Porpoise と同じ）。
  - RADV: `tools/ps5/build_radv.sh`（PS5_Vulkan の tools/build-radv.sh の手順を自前のスクリプトに。PS5_Mesa 1ba9874f、SDK v0.42＋フォーク e105cf6 を build-ps5-radv/sdk に組み立て、ホスト用 mesa_clc を先にビルド）。出力 build-ps5-radv/radv/lib/libvulkan_radeon.ps5.a。リンクは PS5_Vulkan の tools/radv-link.sh に合わせる（--whole-archive、libc++/abi/unwind、clang の builtins、platform 層、--defsym で ps5_* に、fclose/fflush の wrap）。
  - 設計（予定）:
    1. Vulkan の土台（関数表を vkGetInstanceProcAddr から読む、VK_NO_PROTOTYPES）。PS5 は RADV の vk_icdGetInstanceProcAddr、PC はローダー。表示は VK_KHR_display（Porpoise の porpoise_vk.cpp と同じ。sceVideoOutOpen を wrap して RADV のハンドルを使う）。SDL のビデオは dummy にする（SDL が VideoOut を開かないように）。パッドと音は今の SDL のまま。
    2. SDL の描画 API の代わり（frontend の vkrender）: main.cpp・gpucompose.h・pc_launcher・ui が使う SDL_Renderer の関数（CreateTexture/UpdateTexture/RenderCopy/CopyF/CopyEx/FillRect(s)/Clear/Present/ReadPixels/ブレンド3種/拡大の方法）だけを Vulkan で実装し、タイトルのリンクで --wrap して差し替える。呼ぶ側のコードはそのまま。PC（WSL の lavapipe）で SDL のソフトウェア描画と画素を比べて確かめる。
    3. WORLD ON GPU: melonDS/src/GPU3D_WideGpu_Vk.cpp（GL・deko3D と同じ WideGpuBackend）。シェーダーは wide_gpu/*.glsl をそのまま glslang で SPIR-V に（buffer の binding +8、sampler +16、--aml、gl_VertexID→gl_VertexIndex）。Scale[2]=1、Scale[3]=0（deko3D と同じ）。深度・ステンシルは Vulkan 1.3 の動的状態、ブレンドと attr の書き込みマスクはパイプラインを作り分け。Draw の UBO は動的オフセット。WSL の lavapipe で GL 版とヘッドレスで比べる。
  - 進み具合（11/3、実機未確認）:
    - WORLD ON GPU の Vulkan 版（melonDS/src/GPU3D_WideGpu_Vk.cpp、SSR_WIDE_VK）: WSL の lavapipe で GL 版と比べ、ブラック（日本語）のカラクサタウン（bk3.sav＋bk3start.txt、1280x720、--wide-gpu、SSR_WIDE_GPU_API=vulkan）で 921,600 画素中 60〜370 画素だけ違う（ポリゴンの辺の画素の置き方の違い）。y の向きは GL と同じ Scale[2]=-1（Vulkan は y 下向き・行は上から、GL は y 上向き・行は下から読む、で打ち消し合う）。最初に deko3D と同じ +1 にしたら上下逆だった。
    - SDL の描画の代わり（frontend/vkrender.cpp）: tools/vk_render_test.cpp で SDL の OpenGL ES 2 描画（ペイロード版と同じ）と比べて 921,600 画素中 9,458 画素が最大 5 違うだけ（丸め）。SDL 2 には blend mode の分解関数が公開されていないので、SDL_render.c の符号化（色の演算・係数・アルファ）を自前で読む。色なしの三角形は描画の blend mode（SDL と同じ）。
    - タイトルへの組み込み: Ps5::StartPicture（pc_launcher と main.cpp の SDL_Init の前）で VkRender::Start(画面あり)。成功したら SDL_VIDEODRIVER=dummy（SDL が VideoOut を開かない）。失敗したら SDL の ps5 ビデオとソフトウェア描画に戻る（ラップは VkRender::Active() で振り分け、__real_ へ）。シェーダーキャッシュは RADV の既定（/app0/radv-shader-cache、Porpoise と同じ）。
    - リンク: --no-dynamic-linker（弱い未定義を 0 のままにする。この lld は -z nodynamic-undefined-weak を知らず無視していた）。eboot.bin 約 32MB（Porpoise とほぼ同じ）。新しい import は AGC の 5 関数（sceAgcDriverSubmitDcb など）。
    - 実機 1 回目（11/3）: 起動しない。game.log は `ac_compute_device_uuid's output is based on invalid pci bus info.`（RADV の物理デバイス作成の途中）で止まり、クラッシュの記録もない（固まった）。その直後に RADV がするのはシェーダーキャッシュ（disk_cache_create、データベース形式、flock）を開くことで、そのとき MESA_SHADER_CACHE_DIR を /data/pokemon-ds-wide/cache にしていた（Porpoise は既定の /app0/radv-shader-cache）。なので既定に戻した。原因が別でも分かるように: Vulkan の各段階を log に書く（[vulkan] making the instance など）、Ps5Native::Watch が 10 秒たっても終わらない段階のスレッドに SIGUSR1 を送って、止まっている命令のアドレスとスタックの戻り先を書く（リンクマップ build-ps5-native-work/*.map で関数が分かる）、/data/pokemon-ds-wide/vulkan-starting（ビルドの日時）が残っていたら次の起動は Vulkan を使わずプロセッサーで描く（新しいビルドか、印を消せば再び試す）。
    - 確認してほしいこと: 起動して game.log に `[vulkan] PlayStation 5 GPU ...` と `[vkrender] the screen: 3840x2160` が出るか、ゲーム一覧とゲームの画面、ポーズメニューの WORLD ON GPU が ON か、速度。
- ゲーム一覧のメニュー音（11/2）: `frontend/menusound.h`。リストに HG/SS の ROM（IPK*/IPG*）があれば、その SDAT（data/sound/gs_sound_data.sdat）から SEQ_SE_DP_SELECT（カーソル・設定の変更）、SEQ_SE_DP_DECIDE（ゲーム開始）、SEQ_SE_DP_WIN_OPEN（設定などを開く）、SEQ_SE_GS_GEARCANCEL（閉じる）を、DS のサウンドドライバーをなぞって PCM に書き出し、SDL の音声で鳴らす（ゲームの音はリポジトリにも配布物にも入らない）。HGSS のメニューはカーソルも A も B も DP_SELECT（decomp の list_menu_2d.c、options_app.c）、X メニューは開く WIN_OPEN・閉じる GEARCANCEL（start_menu.c）。X メニューのカーソル音は別の音（未特定）。
  - pc_launcher.cpp / switch_platform.cpp: ループの前後で状態（タブ、選択、設定ページの位置と中身、開いている窓）を比べて音を選ぶ。設定 GAME LIST: MENU SOUNDS（list_sounds、既定オン）と SOUND: VOLUME に従う。
  - 合わせ方: `--audio-dump FILE`（テスト用、ゲームの音を生の 16bit ステレオで書き出す）で HG の音を録り、押した版と押さない版の差で効果音だけを取り出して比べた。音の高さは一致。音量は、ベロシティだけ 2乗の曲線（40log）、トラック音量・エクスプレッション・シーケンス音量は 20log にすると WIN_OPEN が時間ごとに 1.4dB 以内で一致（全部 40log だと 11dB 小さく、音量変化が深すぎた）。全体のゲイン 1.65。
  - WSL での確認: PC 全ゲーム版（build-all、SSR_PC_ALL_GAMES）を SDL_AUDIODRIVER=disk と xdotool で動かし、移動5回でカーソル音5回、設定の開閉で WIN_OPEN と GEARCANCEL を確認。
- LAN 通信（11/2、実機未確認）: melonDS 1.1 の LAN（melonDS/src/net/LAN.cpp、ENet、ポート 7064、部屋の告知は UDP 7063、プロトコル版 1）をそのまま使う。本家 melonDS 1.1（2025-11 の最新リリース）と同じなので、PC の melonDS とつながるはず。melonDS の LocalMP は1つのプログラムの中の複数台用なので、プログラム2つの通信には使えない。Android の melonDS（rafaelvcaetano）は README の Missing Features に Local Multiplayer があり、通信できない。
  - ENet 1.3.18 を frontend/thirdparty/enet に入れた（MIT）。CMake で enet と net/LAN.cpp・LocalMP.cpp・MPInterface.cpp をビルド。platform.cpp の MP_* は MPInterface（LAN を使っていなければ何もしない DummyMP）へ。送受信の数を数え、`frontend/lanplay.h` の Process が10秒ごとに game.log へ書く。
  - MAC アドレス: melonDS の既定（00:09:BF:11:22:33）だと相手と同じになるので、設定ファイルの lan_mac（なければ 00:09:BF＋ランダム3バイトを作って追記）を MakeConsole でファームウェアに書く（UpdateChecksums）。ヘッドレスは LAN を使うときだけ。
  - ポーズメニュー LAN 通信（PAGE_LAN）: このDS（名前は機種名＋MAC の末尾）、部屋を作る（4人まで）、部屋に入る（見つかった部屋の一覧、30フレームごとに更新）、抜ける、ホスト中はアドレスと参加者。止まっている間（メニュー）はメインスレッドで、動いている間は runFrames の中で LAN の Process を呼ぶ（同時には呼ばない）。
  - Switch: libnx のソケットは SwitchPlatform::StartNetwork（RetroAchievements と共通）で始める。
  - テスト用 `--lan-host N` / `--lan-join ADDRESS`。WSL で2つのヘッドレスを 127.0.0.1 でつなぎ、2人の部屋になることを確認。ゲーム内の無線も確認: ユーザーの C-Gear 入手後のブラックのセーブ（Downloads/black-jp.sav、WSL の /root/work/bkc.sav）で2つ起動し、続きから始めて C-Gear の電源（下画面右下、タッチ 220,170）→「はい」（タッチ 210,60）にすると、両方が10秒あたり約30パケット送り約20受け取る（LAN なしでは0）。
- 日本語版ブラックでユニオンルームに入ると止まる（11/3、ROM の問題）: PS5 で部屋を作ってユニオンルームに入ると画面が暗いまま。game.log に `ARM9: data abort (0217361A)` と `undefined THUMB9 instruction E92D @ 01F68022`。ユーザーのセーブ（受付の前でレポート）で WSL でも再現し、LAN なし・8MB なし（SSR_B2_MAP=0）・関数表なし・インタプリタでも同じ。ユニオンルームのコードはオーバーレイ12（RAM 0x02172a20、0x2cc0 バイト、BLZ 圧縮）で、ユーザーの ROM（md5 e16fe165…、roms フォルダーの「ポケットモンスターブラック.nds」と PS5 の「NDS-Pokemon Black-JP.nds」は同じ）ではその圧縮データが途中（出力 0x2569、入力 0x1dfb）から壊れている。止まった時点の RAM はオーバーレイ12の展開できる後半と一致し、その先はでたらめだった。ほかの 229 個のオーバーレイ、日本語版ホワイト・ブラック2・英語版ブラックは全部正しく展開できる。直すには正しい吸い出しが要る。ホワイトのオーバーレイ12（0x20 後ろに置かれる同じコード）から作り直す案は、ポインター（+0x20）、本体のデータ（+0x18 など）、BL の飛び先をずれ表で直す必要があり、未着手。調べ方: scratchpad の ovscan.py（全オーバーレイを展開して確かめる）。
- WSL（Ubuntu-24.04）を入れた: `wsl -d Ubuntu-24.04 -u root`。/root/fulldex（Windows のクローンから git clone）、/root/work（ROM とセーブのコピー、IRBJ.txt）。Git Bash からは MSYS_NO_PATHCONV=1 を付けて、スクリプトをファイルで渡す。表だけ＋JIT のビルドは数分。
- 本家（upstream/main）に未取り込みのコミットが8つある（11/1〜2 のホットフィックス：暗い洞窟、フェード、HGSS 図鑑、アップデーターなど）。
- 未確認・今後: ほかの作品（ホワイト、ブラック2、プラチナ、SS/HG）の PS5 での動作、ときどき落ちるフレーム、終了後に黒い画面が残らないか（インタプリタで重い可能性）、JIT（sceKernelJitCreateSharedMemory 等）。

## ROMの場所（ユーザーPC）

- `C:\Users\user\Documents\roms`（すべて日本語版）: ブラック、ホワイト、ブラック2、プラチナ、ソウルシルバー、ハートゴールド。ホワイト2(日本語)は無い。
- `C:\Users\user\Documents\海外roms`（英語版）: ブラック US、ブラック2 US/EU、プラチナ US、ソウルシルバー EU。
- 取り込みはフォルダアクセスをもらって device_stage_files（1ファイル400MBまで。超えるものはZIP化を依頼）。

## 変更した主なファイル（BW1）

| ファイル | 内容 |
|---|---|
| `tools/bw1/match_jp.py` | 英語版ブラックと日本語版ROMを照合して、関数のずれの表（shifts）を作る。マップ拡張用アドレスも探す。`--via-jp-black <black-jp.nds> <white-jp.nds>` で英語版ROMの代わりに日本語版ブラック（jp_black_shifts.txt経由）を基準にできる |
| `tools/bw1/jp_black_shifts.txt` | 日本語版ブラックのずれの表 |
| `tools/bw1/jp_white_shifts.txt` | 日本語版ホワイトのずれの表（--via-jp-black で作成） |
| `tools/bw1/funcmap.py` | `--jp-black` / `--jp-white` オプション |
| `tools/bw2/funcmap.py` | 展開できないオーバーレイを空として扱う（ユーザーの日本語版ブラックの ROM はオーバーレイ12が壊れている。下の「ユニオンルームで止まる」） |
| `frontend/bw1.h`, `exclusives.h`, `launcher.h`, `pc_launcher.cpp`, `switch_platform.cpp` | IRBJ / IRAJ を英語版と同様に扱う。セーブ `black-jp.sav` / `white-jp.sav` |
| `frontend/bw2map.h` | IRBJ: `0x021d3530`, `0x0209d7bc`, `0x199000`, 21 / IRAJ: `0x021d3548`, `0x0209d7d4`, `0x199000`, 21 |
| `tools/switch/build_game.sh` | shifts ファイルがあれば IRBJ / IRAJ の表を romfs に入れる |
| `CHANGELOG.md` | Unreleased に日本語版の行 |

## 照合の方法と分かったこと

- 英語版と同じ関数を、直前の関数のずれを起点に、アドレス等をマスクした命令列で探す（窓を広げながら）。日本語版ブラックは 35,255 関数中 33,065 個が一致。
- 画面レイアウトが使う関数45個はすべて特定済み。うち4個は日本語版で構造体のオフセットが違うため類似度で特定:
  `GFLAppInit` +0x0、`ConfigPanel_Main` ov068 -0x160、`BChoiceMain_Task` ov093 -0x1cc、`NameEntry_ProcMain` ov194 -0x138
- PC版（Linux）で英語版と同じ入力を流して比較し、場面の切り替わりが一致することを確認（ブラック）。
- ホワイト: `--via-jp-black` の自己テスト（日本語版ブラック同士）で jp_black_shifts.txt と全関数一致を確認。ホワイトは 34,674 関数一致、45個の監視関数すべてがマスク後の命令列で日本語版ブラックと同一。bw2map の値はブラック+0x18（英語版の Black→White と同じ関係）、ユーザーヒープ 0x199000。PC版での比較は未実施。

## 次にやること

- PS5 版の実機確認（起動、速度、終了後の戻り方、JIT）。
- PS5 のタイトル版（PKFD00002）の実機確認: タイルから起動、PS ボタンでホームと行き来、ゲーム一覧とゲームが JIT で動くか。だめなら PKFD00099 の四角と native-probe.log を見る。動いたらペイロード版をやめて、タイトル ID をどうするか決める。その後 GPU（Vulkan か静的 Mesa）。
- ホワイト2（IRDJ）: 日本語版ROMが手に入れば IREJ と同じ方法で。

## 注意

- ROMはリポジトリに入れない（公開リポジトリ）。
- aletheia の function_addrs.tsv は raw.githubusercontent.com が使えないので git の sparse-checkout で取る（commit 0d05b6cc…）。
