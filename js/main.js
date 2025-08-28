
const TARGET_FPS = 60;

window.plinst = null;
let canvasl, ctxl;
let canvas, ctx;
let mouseX = -10, mouseY = -10, mouseDL = 0, mouseDR = 0, mousePL = 0, mousePR = 0, lastKey = "";

const initCanvas = () => {
    canvasl = document.getElementById('canvas2d');
    ctxl = canvasl.getContext('2d');
    canvas = document.createElement('canvas');
    canvas.width = 64;
    canvas.height = 64;
    ctx = canvas.getContext('2d');

    window.addEventListener('mousemove', (ev) => {
        let e = ev || window.event;
        const W = canvasl.width, H = canvasl.height;  
        let tsz = Math.min(W, H);
        let cx = W*0.5, cy=H*0.5;
        cx -= tsz * 0.5;
        cy -= tsz * 0.5;
        mouseX = Math.floor((e.clientX - cx) / tsz * 64);
        mouseY = Math.floor((e.clientY - cy) / tsz * 64);
        mouseDL = (e.buttons & 1) ? 1 : 0;
        mouseDR = (e.buttons & 2) ? 1 : 0;
    });

    window.addEventListener('mousedown', (ev) => {
        let e = ev || window.event;
        if (e.buttons & 1) {
            mouseDL = 1;
        }
        if (e.buttons & 2) {
            mouseDR = 1;
        }
    });

    window.addEventListener('mouseup', (ev) => {
        let e = ev || window.event;
        if (!(e.buttons & 1)) {
            mouseDL = 0;
        }
        if (mouseDR && !(e.buttons & 2)) {
            mousePR = 1;
        }
        if (!(e.buttons & 2)) {
            mouseDR = 0;
        }
    });

    let soundsLoaded = false;

    window.SFX = new Array(10);

    window.loadSounds = () => {
        SFX[0] = new Howl({
            src: ['sfx/music.wav'],
            volume: 0.35,
            loop: true
        });
        SFX[0].play();
        SFX[1] = new Howl({
            src: ['sfx/click.wav'],
            volume: 1.
        });
        SFX[2] = new Howl({
            src: ['sfx/die.wav'],
            volume: 0.5
        });
        SFX[3] = new Howl({
            src: ['sfx/drill.wav'],
            volume: 0.5
        });
        SFX[4] = new Howl({
            src: ['sfx/explosion.wav'],
            volume: 0.5
        });
        SFX[5] = new Howl({
            src: ['sfx/lose.wav'],
            volume: 0.5
        });
        SFX[6] = new Howl({
            src: ['sfx/rocket.wav'],
            volume: 0.5
        });
        SFX[7] = new Howl({
            src: ['sfx/safe.wav'],
            volume: 0.5
        });
        SFX[8] = new Howl({
            src: ['sfx/select.wav'],
            volume: 0.5
        });
        SFX[9] = new Howl({
            src: ['sfx/win.wav'],
            volume: 0.5
        });
    }

    window.addEventListener('click', (ev) => {
        let e = ev || window.event;
        if (e.which === 1) {
            mousePL = 1;
        }
        if (e.which === 2) {
            mousePR = 1;
        }
        if (!soundsLoaded) {
            soundsLoaded = WebTransportDatagramDuplexStream;
            loadSounds();
        }
    });

    window.addEventListener('keyup', (ev) => {
        let e = ev || window.event;
        lastKey = e.key;
    })
};

const resizeCanvas = () => {
    canvasl.width = window.innerWidth;
    canvasl.height = window.innerHeight;
};

const redraw = async () => {

    plinst.setMouse(mouseX, mouseY, mouseDL, mouseDR, mousePL, mousePR, lastKey);
    mousePL = mousePR = 0;
    lastKey = "";
    plinst.drawFrame(1./TARGET_FPS);
    let ptr = plinst.getGfxPtr();
    ctx.clearRect(0, 0, 64, 64);
    ctx.putImageData(new ImageData(new Uint8ClampedArray(new Uint8ClampedArray(plinst.memory.buffer).subarray(ptr, ptr + 64*64*4)), 64, 64), 0, 0);
    
};

const tick = async () => {
    setTimeout(tick, 1000/TARGET_FPS);

    resizeCanvas();

    await redraw();

    const W = canvasl.width, H = canvasl.height;

    ctxl.imageSmoothingEnabled = false;
    ctxl.clearRect(0, 0, W, H);
    if (W > H) {
        let tsz = H;
        ctxl.drawImage(canvas, 0, 0, 64, 64, W/2 - tsz/2, 0, tsz, tsz);
    }
    else {
        let tsz = W;
        ctxl.drawImage(canvas, 0, 0, 64, 64, 0, H/2 - tsz/2, tsz, tsz);
    }
};

const loadSpriteSheet = async () => {
    await new Promise((resolve) => {
        const img = new Image();
        img.onload = () => {
            const canvas = document.createElement('canvas');
            canvas.width = img.width;
            canvas.height = img.height;
            const ctx = canvas.getContext('2d');
            ctx.drawImage(img, 0, 0);

            const imageData = ctx.getImageData(0, 0, img.width, img.height);
            const pixelData = imageData.data;

            const numBytes = pixelData.length;
            const ptr = plinst.malloc(numBytes);
            plinst.HEAPU8.set(pixelData, ptr);

            plinst.setSpriteSheet(ptr, img.width, img.height);
            resolve();
        };
        img.src = 'img/spr.png';
    });
};
  
const loadLevels = async () => {
    await new Promise((resolve) => {
        fetch('levels/levels.b64l')
            .then(r => r.text())
            .then(t => {
                window.levelB64 = t.split('\n');
                resolve();
            })
    });
}

window.main = async () => {
    plinst = await plibWrapper();

    initCanvas();
    //let x = plinst.addTwo(1,2);
    //console.log(x);

    await loadSpriteSheet();
    await loadLevels();

    plinst.init();

    if (!plinst.editorMode()) {
        _loadLevel(10, true);
    }

    tick();
};