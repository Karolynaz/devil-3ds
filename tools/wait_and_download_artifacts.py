#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile

RUN_ID = sys.argv[1] if len(sys.argv) > 1 else None
TARGET_DIR = sys.argv[2] if len(sys.argv) > 2 else "3DS_Release"

res = subprocess.run(['git', 'credential', 'fill'], input=b'protocol=https\nhost=github.com\n\n', capture_output=True)
token = None
for line in res.stdout.decode().splitlines():
    if line.startswith('password='):
        token = line.split('=', 1)[1]

if not token:
    print("Error: Could not retrieve GitHub token from git credentials")
    sys.exit(1)

headers = {
    'Authorization': f'token {token}',
    'User-Agent': 'Devil3DS-Agent',
    'Accept': 'application/vnd.github.v3+json'
}

class NoAuthRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new_req = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new_req is not None and 'Authorization' in new_req.headers:
            del new_req.headers['Authorization']
        return new_req

opener = urllib.request.build_opener(NoAuthRedirect)

if not RUN_ID:
    req = urllib.request.Request("https://api.github.com/repos/Karolynaz/devil-3ds/actions/runs?per_page=1", headers=headers)
    with opener.open(req) as resp:
        data = json.load(resp)
        RUN_ID = str(data['workflow_runs'][0]['id'])

print(f"Monitoring GitHub Actions run {RUN_ID}...")
start_time = time.time()
conclusion = None
head_sha = "latest"

while True:
    req = urllib.request.Request(f"https://api.github.com/repos/Karolynaz/devil-3ds/actions/runs/{RUN_ID}", headers=headers)
    try:
        with opener.open(req) as resp:
            data = json.load(resp)
            status = data.get('status')
            conclusion = data.get('conclusion')
            head_sha = data.get('head_sha', 'latest')[:7]
            elapsed = int(time.time() - start_time)
            print(f"[{elapsed}s] Status: {status}, Conclusion: {conclusion}")
            if status == "completed":
                break
    except Exception as e:
        print(f"Polling error: {e}")
    time.sleep(15)

if conclusion != "success":
    print(f"Workflow finished with unsuccessful conclusion: {conclusion}")
    sys.exit(1)

print("Workflow completed successfully! Fetching artifacts...")
req = urllib.request.Request(f"https://api.github.com/repos/Karolynaz/devil-3ds/actions/runs/{RUN_ID}/artifacts", headers=headers)
with opener.open(req) as resp:
    artifacts_data = json.load(resp)

artifacts = artifacts_data.get('artifacts', [])
print(f"Found {len(artifacts)} artifacts:")
for a in artifacts:
    print(f" - {a.get('name')} ({a.get('size_in_bytes')} bytes)")

version_dir = os.path.join(TARGET_DIR, f"{time.strftime('%Y-%m-%d')}_{head_sha}")
os.makedirs(version_dir, exist_ok=True)
os.makedirs(TARGET_DIR, exist_ok=True)

for a in artifacts:
    name = a.get('name')
    artifact_id = a.get('id')
    download_url = f"https://api.github.com/repos/Karolynaz/devil-3ds/actions/artifacts/{artifact_id}/zip"
    zip_path = os.path.join(TARGET_DIR, f"{name}.zip")
    print(f"Downloading {name}...")
    
    download_req = urllib.request.Request(download_url, headers=headers)
    with opener.open(download_req) as d_resp, open(zip_path, 'wb') as out_file:
        shutil.copyfileobj(d_resp, out_file)
    
    print(f"Extracting {zip_path}...")
    with zipfile.ZipFile(zip_path, 'r') as z:
        z.extractall(version_dir)
        z.extractall(TARGET_DIR)
    
    os.remove(zip_path)

print(f"Artifacts successfully downloaded and extracted to:")
print(f"  {TARGET_DIR}/devil-3ds.3dsx")
print(f"  {TARGET_DIR}/devil-3ds.cia")
print(f"  {version_dir}/")
