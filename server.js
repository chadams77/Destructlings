// server.js
const express = require('express');
const path = require('path'); // Required for path.join

const app = express();
const port = 3000;

// Serve static files from the 'public' directory
app.use(express.static(path.join(__dirname, 'build'), {
    setHeaders: function(res, path) {
        res.set("Cross-Origin-Opener-Policy", "same-origin");
        res.set("Cross-Origin-Embedder-Policy", "require-corp");
    }
}));

// Start the server
app.listen(port, () => {
    console.log(`Static server listening at http://localhost:${port}`);
});