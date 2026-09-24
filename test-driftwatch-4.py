import subprocess


def run_backup(target_dir, use_shell=False):
    subprocess.run(["backup-tool", target_dir], check=True)
