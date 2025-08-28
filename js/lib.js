window._playSound = (val) => {
    if (window.SFX[val]) {
        window.SFX[val].play();
    }
};

window.plibWrapper = async () => {
    const pl = await plib();
    return {
        pl,
        memory: pl.wasmMemory,
        setMouse: pl.cwrap('setMouse', 'int', ['int', 'int', 'int', 'int', 'int', 'int', 'string']),
        init: pl.cwrap('init', 'void', []),
        getGfxPtr: pl.cwrap('getGfxPtr', 'int', []),
        getSaveBfr: pl.cwrap('getSaveBfr', 'int', []),
        getSaveBfrSz: pl.cwrap('getSaveBfrSz', 'int', []),
        loadLevelEditor: pl.cwrap('loadLevelEditor', 'void', []),
        initTitle: pl.cwrap('initTitle', 'void', ['bool']),
        editorMode: pl.cwrap('editorMode', 'bool', []),
        playLevel: pl.cwrap('playLevel', 'void', []),
        drawFrame: pl.cwrap('drawFrame', 'void', ['float']),
        malloc: pl.cwrap('allocate', 'int', ['int']),
        HEAPU8: pl.HEAPU8,
        setSpriteSheet: pl.cwrap('setSpriteSheet', 'void', ['unsigned char *', 'int', 'int'])
    };
};

window._editorLoadLevel = (levelNo) => {
    let ptr = plinst.getSaveBfr();
    let size = plinst.getSaveBfrSz();
    let str = window.localStorage.getItem('level-' + levelNo);
    if (!str) {
        return;
    }
    let bfr = new Uint8Array(atob(str).split("").map((c) => c.charCodeAt(0)));
    plinst.HEAPU8.set(bfr, ptr);
    plinst.loadLevelEditor();
};

window._loadLevel = (levelNo, isTitle, isIntro) => {
    let ptr = plinst.getSaveBfr();
    let size = plinst.getSaveBfrSz();
    let str = window.levelB64[levelNo-1];
    if (!str) {
        return;
    }
    let bfr = new Uint8Array(atob(str).split("").map((c) => c.charCodeAt(0)));
    plinst.HEAPU8.set(bfr, ptr);
    if (!isTitle) {
        plinst.playLevel();
    }
    else {
        plinst.initTitle(true);
    }
};

window._getGamePos = () => {
    return window.localStorage.getItem('phl') ? parseInt(window.localStorage.getItem('phl')) : 0;
}

window._setLevelRes = (levelNo, win) => {
    if (win) {
        let gp = _getGamePos();
        window.localStorage.setItem('phl', "" + (Math.max(gp, levelNo)));
    }
}

window._editorSaveLevel = (levelNo) => {
    let ptr = plinst.getSaveBfr();
    let size = plinst.getSaveBfrSz();
    let bfr = new Uint8ClampedArray(plinst.memory.buffer).subarray(ptr, ptr + size);
    var str = '';
    for (let i=0; i<bfr.length; i++) {
        str += String.fromCharCode(bfr[i]);
    }
    let b64encoded = btoa(str);
    window.localStorage.setItem('level-' + levelNo, b64encoded);
};

window.downloadLevels = () => {
    let element = document.createElement('a');
    let text = [];
    for (let i=1; i<=10; i++) {
        let line = window.localStorage.getItem('level-' + i);
        if (line === null || line == undefined) {
            line = "";
        }
        text.push(line);
    }
    text = text.join("\n");
    element.setAttribute('href', 'data:text/plain;charset=utf-8,' + encodeURIComponent(text));
    element.setAttribute('download', "levels.b64l");

    element.style.display = 'none';
    document.body.appendChild(element);

    element.click();

    document.body.removeChild(element);
}