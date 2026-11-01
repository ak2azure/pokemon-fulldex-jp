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
- 未確認・今後: ほかの作品（ホワイト、ブラック2、プラチナ、SS/HG）の PS5 での動作、ときどき落ちるフレーム、終了後に黒い画面が残らないか（インタプリタで重い可能性）、終了後リストへ戻る方法、JIT（sceKernelJitCreateSharedMemory 等）。

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
| `tools/bw2/funcmap.py` | 展開できないオーバーレイを空として扱う（日本語版ブラックのオーバーレイ12が壊れていて未使用） |
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
- ホワイト2（IRDJ）: 日本語版ROMが手に入れば IREJ と同じ方法で。

## 注意

- ROMはリポジトリに入れない（公開リポジトリ）。
- aletheia の function_addrs.tsv は raw.githubusercontent.com が使えないので git の sparse-checkout で取る（commit 0d05b6cc…）。
