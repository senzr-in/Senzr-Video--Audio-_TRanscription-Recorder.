from flask import Flask
from backend.api.status import status_bp
from backend.api.wifi import wifi_bp
from backend.api.inference import inference_bp


def create_app():
    app = Flask(__name__)

    # Register blueprints — each blueprint is a group of related routes.
    # This keeps route files small and focused.
    app.register_blueprint(status_bp, url_prefix="/api")
    app.register_blueprint(wifi_bp, url_prefix="/api/wifi")
    app.register_blueprint(inference_bp, url_prefix="/api/inference")

    return app


if __name__ == "__main__":
    app = create_app()
    # debug=True → auto-reloads on code change. Only for development.
    app.run(host="0.0.0.0", port=5000, debug=True)