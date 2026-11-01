POKEMON FULLDEX FOR PS5 (TEST VERSION)

For a jailbroken PS5 running ps5-payload-websrv (sent to elfldr, port 9021). Pokemon Black,
White, Black 2, White 2, SoulSilver, HeartGold and Platinum, the Japanese ones too, from your
own ROMs.

1. Copy the folder homebrew/PokemonFulldex to the PS5 (an FTP server payload such as ftpsrv),
   so that it is /data/homebrew/PokemonFulldex, with eboot.elf in it.
2. Copy the folder pokemon-ds-wide to /data, so that it is /data/pokemon-ds-wide, with
   data/funcs in it, and put your ROMs (.nds) in /data/pokemon-ds-wide/roms.
3. Start websrv (send websrv-ps5.elf to elfldr), then open its homebrew list: the Homebrew
   Launcher on the PS5, or http://<PS5's address>:8080 in a browser. Pick Pokemon Fulldex; the
   game list comes up.

Do not send eboot.elf to elfldr by itself: started that way it cannot show its picture.
A notification in the corner of the screen says what went wrong if it cannot start.

Saves go in /data/pokemon-ds-wide/saves, the settings in settings.cfg and the log in game.log
beside them (send game.log with a bug report). QUIT (or CHANGE GAME) in the pause menu ends the
program: start it again from the homebrew list for the game list.

This version draws the picture with the PS5's processor and runs the games on a JIT (on the
interpreter if the console does not allow it; game.log says which), so some places may be slow.

----

PS5 版 (テスト版)

jailbreak した PS5 で ps5-payload-websrv (elfldr のポート 9021 に送る) から起動します。

1. homebrew/PokemonFulldex フォルダーを PS5 にコピーします (ftpsrv などの FTP)。
   /data/homebrew/PokemonFulldex/eboot.elf となるように。
2. pokemon-ds-wide フォルダーを /data にコピーし (/data/pokemon-ds-wide/data/funcs となるように)、
   自分の ROM (.nds) を /data/pokemon-ds-wide/roms に入れます。
3. websrv を起動し (websrv-ps5.elf を elfldr に送る)、ホームブリューの一覧を開きます
   (PS5 の Homebrew Launcher、またはブラウザで http://<PS5のアドレス>:8080)。
   Pokemon Fulldex を選ぶとゲームリストが出ます。

eboot.elf を elfldr に直接送らないでください。その起動方法では画面を出せません。
起動できないときは、画面の隅の通知に理由が出ます。

セーブは /data/pokemon-ds-wide/saves、設定は settings.cfg、ログは game.log です。
ポーズメニューの終了 (ゲーム変更) でプログラムが終わります。リストに戻るにはホームブリューの一覧からもう一度起動してください。
この版は画面を CPU で描き、JIT で動きます (使えない本体ではインタプリタ。どちらかは game.log に出ます)。重い場面があるかもしれません。
