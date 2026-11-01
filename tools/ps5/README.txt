POKEMON FULLDEX FOR PS5 (TEST VERSION)

For a jailbroken PS5 with an ELF loader (elfldr, port 9021). Pokemon Black, White, Black 2,
White 2, SoulSilver, HeartGold and Platinum, the Japanese ones too, from your own ROMs.

1. Copy the folder pokemon-ds-wide to /data on the PS5 (an FTP server payload such as ftpsrv),
   so that it is /data/pokemon-ds-wide, with data/funcs in it.
2. Put your ROMs (.nds) in /data/pokemon-ds-wide/roms.
3. Send pokemon-fulldex.elf to the PS5 (Payload Manager, PS5Upload, or
   nc <PS5's address> 9021 < pokemon-fulldex.elf). The game list comes up; pick a game.

Saves go in /data/pokemon-ds-wide/saves, the settings in settings.cfg and the log in game.log
beside them (send game.log with a bug report). QUIT (or CHANGE GAME) in the pause menu ends the
program: send it again for the list.

This first version draws the picture with the PS5's processor and runs the games without a JIT,
so some places may be slow.

----

PS5 版 (テスト版)

1. pokemon-ds-wide フォルダーを PS5 の /data にコピーします (ftpsrv などの FTP)。
   /data/pokemon-ds-wide/data/funcs となるように。
2. 自分の ROM (.nds) を /data/pokemon-ds-wide/roms に入れます。
3. pokemon-fulldex.elf を PS5 に送ります (Payload Manager、PS5Upload、ポート 9021)。
   ゲームリストが出るので、ゲームを選びます。

セーブは /data/pokemon-ds-wide/saves、設定は settings.cfg、ログは game.log です。
ポーズメニューの終了 (ゲーム変更) でプログラムが終わります。リストに戻るにはもう一度送ってください。
この版は画面を CPU で描き、JIT なしで動くため、重い場面があるかもしれません。
