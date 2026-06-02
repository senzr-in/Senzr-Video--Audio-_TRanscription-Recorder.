import subprocess


def run_command(command: list[str]):
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        return {
            "ok": True,
            "stdout": result.stdout.strip(),
            "stderr": result.stderr.strip()
        }
    except subprocess.CalledProcessError as e:
        return {
            "ok": False,
            "stdout": e.stdout.strip() if e.stdout else "",
            "stderr": e.stderr.strip() if e.stderr else ""
        }


def nginx_status():
    return run_command(["systemctl", "is-active", "nginx"])


def restart_nginx():
    return run_command(["sudo", "systemctl", "restart", "nginx"])


def flask_process_status():
    return run_command(["pgrep", "-af", "python -m backend.app"])