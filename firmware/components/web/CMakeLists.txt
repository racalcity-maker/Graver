idf_component_register(
    SRCS
        "src/web_api.cpp"
        "src/web_pages.cpp"
        "src/web_server.cpp"
    INCLUDE_DIRS "include"
    REQUIRES control esp_http_server jobs json network shared storage
    EMBED_TXTFILES
        "ui/index.html"
        "ui/styles.css"
        "ui/api.js"
        "ui/app.js"
        "ui/upload.js"
        "ui/image/rasterize.js"
        "ui/image/primitives.js"
        "ui/image/stroke_font.js"
)
