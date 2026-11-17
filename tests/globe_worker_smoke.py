import json
import subprocess
import sys


request = {
    "schemaVersion": 1,
    "observer": {
        "latitudeDeg": 78.923,
        "longitudeDeg": 11.923,
        "ellipsoidalHeightM": 50.0,
        "minElevationDeg": 10.0,
    },
    "targets": [],
    "includeTrajectories": True,
}
completed = subprocess.run(
    [sys.argv[1], sys.argv[2]], input=json.dumps(request) + "\n",
    text=True, capture_output=True, check=True, timeout=10,
)
response = json.loads(completed.stdout)
assert response["schemaVersion"] == 1
assert response["ok"] is True
assert response["targets"] == []
assert response["snapshots"] == []
assert response["observer"] == request["observer"]
assert response["epochUtc"].endswith("Z")
