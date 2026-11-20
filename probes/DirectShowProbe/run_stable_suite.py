"""Stable evidence validation. Missing suites/evidence never count as passing."""
import argparse
import json
from pathlib import Path
import time
import uuid

def load_matrix(path: Path) -> dict:
    return json.loads(path.read_text(encoding='utf-8'))

def create_attempt(root: Path, suite: str) -> Path:
    if suite not in {'smoke','accuracy','faults','soak','package','ui'}:
        raise ValueError('Unknown suite')
    path=root/f'{suite}-{time.time_ns()}-{uuid.uuid4().hex[:8]}'
    path.mkdir(parents=True, exist_ok=False)
    return path

def validate_result(result: dict) -> list[str]:
    errors=[]
    for key in ('player_sha256','expected_player_sha256','collector_sha256','bridge_sha256','config_sha256'):
        value=result.get(key)
        if not isinstance(value,str) or len(value)!=64 or any(c not in '0123456789abcdef' for c in value):
            errors.append('invalid '+key)
    if result.get('player_sha256')!=result.get('expected_player_sha256'):
        errors.append('player fingerprint mismatch')
    witness=result.get('witness')
    if not isinstance(witness,dict): errors.append('missing witness')
    else:
        width=witness.get('uncertainty_ms')
        if not isinstance(width,(int,float)) or not 0<=width<=100:
            errors.append('witness not calibrated within 100ms')
    checks=result.get('checks')
    if not isinstance(checks,dict) or not checks: errors.append('missing checks')
    elif any(value is not True for value in checks.values()): errors.append('checks failed or not run')
    return errors

def validate_switch(row: dict) -> list[str]:
    errors=[]
    if row.get('source')!=row.get('expected_source'):errors.append('source did not switch')
    if abs(row.get('duration_ms',-9999)-row.get('expected_duration_ms',0))>100:errors.append('player duration did not switch')
    health=row.get('health') or {}
    if health.get('workers')!=4 or not 1<=health.get('slots',0)<=4 or not health.get('enabled') or any(health.get(k,1)!=0 for k in ['failures','rejected','lost']):
        errors.append('collector inactive or unhealthy')
    return errors

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--suite',required=True,choices=['smoke','accuracy','faults','soak','package','ui'])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--player',type=Path,required=True)
    args=parser.parse_args()
    attempt=create_attempt(args.output,args.suite)
    report={'suite':args.suite,'player':str(args.player.resolve()),'status':'NOT_RUN',
            'reason':'Suite implementation/independent evidence not yet available','deployment_eligible':False}
    (attempt/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(attempt)
    return 2

if __name__=='__main__': raise SystemExit(main())
