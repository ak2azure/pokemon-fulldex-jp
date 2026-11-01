// websrv's homebrew list (ps5-payload-websrv): the name shown, and the payload started.
async function main() {
    return {
        mainText: "Pokémon Fulldex",
        secondaryText: "Pokémon DS games, widescreen (test version)",
        onclick: async () => {
            return {
                path: window.workingDir + "/eboot.elf"
            };
        }
    };
}
