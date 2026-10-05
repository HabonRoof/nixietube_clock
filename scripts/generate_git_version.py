import subprocess

revision = (
    subprocess.check_output(["git", "rev-parse", "HEAD"])
    .strip()
    .decode("utf-8")
)

print(f"-DGIT_COMMIT_HASH='\"{revision}\"'")