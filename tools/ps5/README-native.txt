POKEMON FULLDEX FOR PS5, AS A TITLE OF ITS OWN (TEST VERSION)

For a jailbroken PS5 with etaHEN (with kstuff) and ShadowMountPlus. This version is a title
like any game: it starts straight from its Home screen tile, and the PS button goes to the
Home screen and back. No ELF loader, websrv or payload on the way.

1. Copy the folder PKFD00002 to the PS5 (FTP, for example etaHEN's FTP server, in binary
   mode), so that it is /data/homebrew/PKFD00002 with eboot.bin in it. ShadowMountPlus adds
   "Pokémon Fulldex Native" to the Home screen.
2. If you do not have it yet, copy the folder pokemon-ds-wide to /data, so that it is
   /data/pokemon-ds-wide with data/funcs in it, and put your ROMs (.nds) in
   /data/pokemon-ds-wide/roms. (It is the same folder as the other version's: the ROMs, saves
   and settings are shared.)
3. Start Pokémon Fulldex Native from the Home screen.

It asks etaHEN to let it reach /data when it starts. If the corner of the screen says /data
cannot be reached, etaHEN is not running or did not free the title: its log is then in the
title's own storage, not in /data.

The picture is drawn by the processor at 1920x1080 in this version (the graphics chip's 4K
picture is not in it yet). The games run on the JIT. Saves go in /data/pokemon-ds-wide/saves,
the log in /data/pokemon-ds-wide/game.log (send it with a bug report). QUIT in the pause menu
goes back to the game list.

----

PS5 版 (タイトル版、テスト版)

jailbreak した PS5 (etaHEN と kstuff) と ShadowMountPlus が必要です。この版は普通のゲームと
同じタイトルとして動きます。ホーム画面のタイルからそのまま起動し、PS ボタンでホーム画面に
戻って、また戻ってこられます。ELF ローダーや websrv、ペイロードは使いません。

1. PKFD00002 フォルダーを PS5 にコピーします (etaHEN の FTP などで、バイナリモード)。
   /data/homebrew/PKFD00002 の中に eboot.bin がある形にします。ShadowMountPlus がホーム画面に
   「Pokémon Fulldex Native」を追加します。
2. まだなければ pokemon-ds-wide フォルダーを /data にコピーし (/data/pokemon-ds-wide/data/funcs
   となるように)、自分の ROM (.nds) を /data/pokemon-ds-wide/roms に入れます。もう一つの版と
   同じフォルダーなので、ROM・セーブ・設定は共通です。
3. ホーム画面の Pokémon Fulldex Native を起動します。

起動時に etaHEN に /data を使えるように頼みます。画面の隅に /data に届かないと出たら、etaHEN が
動いていないか、タイトルを解放しませんでした (そのときログは /data ではなくタイトル自身の
保存領域に書かれます)。

この版の画面はプロセッサーが 1920x1080 で描きます (グラフィックスチップによる 4K はまだ
入っていません)。ゲームは JIT で動きます。セーブは /data/pokemon-ds-wide/saves、ログは
/data/pokemon-ds-wide/game.log です (不具合の報告にはこれを付けてください)。ポーズメニューの
QUIT でゲームリストに戻ります。
