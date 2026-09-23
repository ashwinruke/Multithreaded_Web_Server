import subprocess

def run_backup(target_dir):
    subprocess.run(target_dir, shell=True)
