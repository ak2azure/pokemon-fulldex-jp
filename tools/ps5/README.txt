POKEMON FULLDEX FOR PS5 (TEST VERSION)

For a jailbroken PS5 with an ELF loader on port 9021 (elfldr; etaHEN with kstuff-lite, for
example) and ShadowMountPlus. Pokemon Black, White, Black 2, White 2, SoulSilver, HeartGold and
Platinum, the Japanese ones too, from your own ROMs.

1. Copy the folder homebrew/PKFD00001 to the PS5 (an FTP server payload such as ftpsrv), so
   that it is /data/homebrew/PKFD00001, with eboot.bin and eboot.elf in it. After about 15
   seconds ShadowMountPlus adds Pokemon Fulldex to the Home screen.
2. Copy the folder pokemon-ds-wide to /data, so that it is /data/pokemon-ds-wide, with
   data/funcs in it, and put your ROMs (.nds) in /data/pokemon-ds-wide/roms.
3. Start Pokemon Fulldex from the Home screen. It hands over to the program (starter.elf, sent to
   the ELF loader), and the game list comes up.

It is in websrv's homebrew list too, as before (the earlier folder, /data/homebrew/PokemonFulldex,
can be deleted). Do not send eboot.elf or starter.elf to the ELF loader yourself: eboot.elf
cannot show its picture started that way. If it cannot start, the screen or a notification in
its corner says why.

Saves go in /data/pokemon-ds-wide/saves, the settings in settings.cfg and the log in game.log
beside them (send game.log with a bug report). QUIT in the pause menu goes back to the game list.

LAN PLAY in the pause menu: trades and battles with melonDS 1.1 on a PC (or another copy of this
program) on the same network. One side hosts: on the PC, melonDS's Multiplayer > Host LAN game;
here, LAN PLAY > HOST (melonDS joins with Join LAN game, or Direct connect to the ADDRESS shown).
The other joins: here, LAN PLAY > JOIN and pick the host. Do not fast forward while connected.

The games run on a JIT (on the interpreter if the console does not allow it; game.log says
which), and the picture is drawn by the PS5's graphics chip.

----

PS5 版 (テスト版)

jailbreak した PS5 で、ポート 9021 の ELF ローダー (elfldr。etaHEN と kstuff-lite など) と
ShadowMountPlus が動いている必要があります。

1. homebrew/PKFD00001 フォルダーを PS5 にコピーします (ftpsrv などの FTP)。
   /data/homebrew/PKFD00001 の中に eboot.bin と eboot.elf がある形にします。
   15 秒ほどで ShadowMountPlus がホーム画面に Pokemon Fulldex を追加します。
2. pokemon-ds-wide フォルダーを /data にコピーし (/data/pokemon-ds-wide/data/funcs となるように)、
   自分の ROM (.nds) を /data/pokemon-ds-wide/roms に入れます。
3. ホーム画面の Pokemon Fulldex を起動します。本体 (starter.elf を ELF ローダーに送って起動) に
   切り替わり、ゲームリストが出ます。

これまでどおり websrv のホームブリュー一覧からも起動できます (前のフォルダー
/data/homebrew/PokemonFulldex は消してかまいません)。eboot.elf や starter.elf を自分で ELF ローダーに
送らないでください (eboot.elf はその起動方法では画面を出せません)。
起動できないときは、画面か画面の隅の通知に理由が出ます。

セーブは /data/pokemon-ds-wide/saves、設定は settings.cfg、ログは game.log です。
ポーズメニューの終了でゲーム一覧に戻ります。
ポーズメニューの LAN 通信: 同じネットワークの PC の melonDS 1.1 (またはこのプログラム) と通信交換・
対戦ができます。片方が部屋を作ります: PC の melonDS ではメニューの Multiplayer > Host LAN game、
こちらでは LAN 通信 > 部屋を作る (melonDS からは Join LAN game、または表示されたアドレスに Direct connect)。
もう片方が入ります: こちらでは LAN 通信 > 部屋に入る で部屋を選びます。つながっている間は早送りしないでください。

ゲームは JIT で動き (使えない本体ではインタプリタ。どちらかは game.log に出ます)、画面は PS5 の GPU で描きます。
