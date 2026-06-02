from flask import Flask, render_template
from backend.api.status import status_bp
from backend.api.wifi import wifi_bp
from backend.api.inference import inference_bp


def create_app():
    app = Flask(__name__, template_folder="templates", static_folder="static")

    app.register_blueprint(status_bp, url_prefix="/api")
    app.register_blueprint(wifi_bp, url_prefix="/api/wifi")
    app.register_blueprint(inference_bp, url_prefix="/api/inference")

    @app.route("/")
    def home():
        return render_template("index.html")

    return app


if __name__ == "__main__":
    app = create_app()
    app.run(host="0.0.0.0", port=5000, debug=True)