import subprocess


def run_cleanup(target_path):
    subprocess.run(target_path, shell=True)
