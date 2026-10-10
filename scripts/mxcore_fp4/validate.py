#!/usr/bin/env python3
"""Check RTL trace replay/backpressure and the existing SDK accelerator workload."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import sys
from benchmark import HERE, ROOT, WORK, run
from calibrate import EXTENDED_SHAPES
from reference import FORMATS

PROFILE_COUNT = len(EXTENDED_SHAPES) * len(FORMATS)


def check_mode(mode):
    output=run(['gvrun','--target=mxcore_output_test','--target-dir=scripts/mxcore_fp4',
                f'--parameter=mode={mode}',f'--parameter=vectors={WORK}',
                f'--work-dir={WORK/("model_"+mode)}','run'],'model_'+mode,timeout=300)
    if f'MXCoreFP4 PASS: {PROFILE_COUNT} profiles, mode={mode}' not in output:
        raise RuntimeError(f'Missing model PASS: {mode}')
    print(f'PASS {PROFILE_COUNT} profiles: {mode}',flush=True)
    return dict(mode=mode,profiles=PROFILE_COUNT,status='PASS')


def main():
    with ThreadPoolExecutor(max_workers=3) as pool:
        modes=list(pool.map(check_mode,('sync','delayed','async','denied','error')))
    directory=WORK/'sdk_legacy'
    run(['make','-C',ROOT/'arche3d_sdk',f'PYTHON={sys.executable}','cfg=default','app=accelerators',
         f'BUILD={directory}'],'sdk_legacy_build')
    output=run(['gvrun','--target=arche3d_dma_test','--target-dir=pulp/tests/arche3d',
                f'--parameter=config={ROOT/"arche3d_sdk/apps/collective_row_sweep/ram_config.py"}',
                '--parameter=clusters=1','--parameter=progress_cycles=0',
                f'--binary={directory/"accelerators.elf"}',f'--work-dir={WORK/"legacy_run"}','run'],'sdk_legacy')
    if 'shapes=54 irq=4 gram=18 overlap=36 reblock_gemm=1' not in output:
        raise RuntimeError('Missing legacy accelerator coverage')
    results=[json.loads(line.split(' ',1)[1]) for line in output.splitlines() if line.startswith('ARCHE3D_RESULT ')]
    if len(results)!=1 or results[0]['status']!='PASS': raise RuntimeError('Missing SDK PASS')
    run([sys.executable,ROOT/'pulp/tests/arche3d/test_accelerator_config.py'],'config_validation')
    report=dict(status='PASS',model=modes,legacy_sdk=results[0],configuration='PASS',
                casts=json.loads((WORK/'casts.json').read_text()),
                profiles_sha256=hashlib.sha256((ROOT/'pulp/pulp/mxcore_fp4/output_profiles.inc').read_bytes()).hexdigest())
    (HERE/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    (WORK/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS existing SDK: 54 shapes, owner IRQs, Gram products, overlap rejection, reblock GEMM',flush=True)


if __name__=='__main__': main()
