#!/usr/bin/env python3
"""Physical-board tests; --oracle also compares NumPy FFT + desktop LiteRT.

Tests inference plumbing/numerics, not the accuracy of human gesture labels.
"""
import argparse
import datetime
import json
import math
import struct
import sys
import time
import zlib
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/python'))
from psoc_e84_tools.serial_console import LineConsole

LABELS = ['unlabelled', 'Circle', 'Push']

def fields(line):
    return dict(item.split('=', 1) for item in line.split() if '=' in item)

def check(condition, message):
    if not condition:
        raise AssertionError(message)

def crc(raw):
    return f'{zlib.crc32(struct.pack("<" + "H" * len(raw), *raw)):08x}'

class Board(LineConsole):
    def __init__(self, port, log):
        super().__init__(serial.Serial(port, 115200, timeout=0.1, exclusive=True), log)
        self.sequence = 0

    def ask(self, command, terminal, timeout=10, success=True):
        self.sequence += 1
        token = f't{self.sequence}'
        start = len(self.lines)
        self.send(f'ai {command} {token}')
        line = self.wait(lambda s: fields(s).get('token') == token and
                         (s.startswith('FAIL ') or s.startswith(terminal)), timeout)
        if success:
            check(line.startswith('OK '), line)
        return fields(line), [s for s in self.lines[start:] if fields(s).get('token') == token]

def verify_score(line):
    f = fields(line)
    scores = [struct.unpack('<f', struct.pack('<I', int(x, 16)))[0] for x in f['bits'].split(',')]
    check(len(scores) == 3 and all(math.isfinite(x) and 0 <= x <= 1 for x in scores), line)
    check(abs(sum(scores) - 1) < 0.001, line)
    check(LABELS[max(range(3), key=scores.__getitem__)] == f['class'], line)
    check(all(abs(int(f[f'p{i}']) - scores[i] * 1_000_000) <= 2 for i in range(3)), line)
    return scores

class Oracle:
    def __init__(self, model):
        import numpy as np
        from ai_edge_litert.interpreter import Interpreter, OpResolverType
        self.np = np
        self.interpreter = Interpreter(model_path=str(model), num_threads=1,
            experimental_op_resolver_type=OpResolverType.BUILTIN_REF)
        self.interpreter.allocate_tensors()
        self.input = self.interpreter.get_input_details()[0]
        self.output = self.interpreter.get_output_details()[0]
        check(list(self.input['shape']) == [1, 10, 65], 'Unexpected model input shape')

    def predict(self, raw):
        np = self.np
        # The export selects offset 1 of the real/imag pairs: IMAGINARY FFT,
        # not magnitude. No ADC normalization/windowing is added.
        features = np.fft.rfft(np.asarray(raw, dtype=np.float32).reshape(10, 128), axis=1).imag
        self.interpreter.set_tensor(self.input['index'], features.astype(np.float32)[None])
        self.interpreter.invoke()
        return self.interpreter.get_tensor(self.output['index'])[0].tolist()

def compare(actual, expected):
    error = max(abs(a - b) for a, b in zip(actual, expected))
    check(error < 0.002, f'On-board versus desktop model mismatch: {actual} vs {expected}, error={error}')
    return error

def info(board, args, results):
    data, _ = board.ask('info', 'OK AI INFO')
    check(data['chapter'] == '022' and data['classes'].split(',') == LABELS, data)
    check(data['model'] == '850a926a-3fd3-4acd-9280-478d7a438955', data)
    check(data['adc_bits'] == '12' and data['frames_per_window'] == '10', data)
    check(0 < int(data['arena']) <= 16384 and data['active'] == '0', data)
    results['info'] = data

def selftest(board, args, results):
    data, lines = board.ask('selftest', 'OK AI SELFTEST', timeout=20)
    rows = [s for s in lines if s.startswith('DATA AI SELF ')]
    check(len(rows) == 6 and data['repeat_exact'] == '1', lines)
    check(data['warmup_checked'] == '1' and data['guards_checked'] == '1', data)
    errors = []
    for p in range(2):
        oracle = Oracle(args.model) if args.oracle else None
        for w in range(3):
            line = rows[p * 3 + w]
            f = fields(line)
            check(int(f['pass']) == p and int(f['window']) == w, line)
            actual = verify_score(line)
            raw = [1024 + ((frame * 37 + i * 53 + (i * i) % 97) % 2048)
                   for frame in range(w * 10, w * 10 + 10) for i in range(128)]
            check(crc(raw) == f['crc'], line)
            check(f['bits'] == fields(rows[w])['bits'], 'Reset not reproducible')
            if oracle:
                errors.append(compare(actual, oracle.predict(raw)))
    results['selftest'] = {'windows': 6, 'repeat_exact': True, 'oracle_errors': errors,
                           'scores': [verify_score(row) for row in rows]}

def live(board, args, results):
    snapshots = []
    for _ in range(2):
        done, lines = board.ask('run 1', 'OK AI DONE', timeout=10)
        rows = [s for s in lines if s.startswith('DATA AI RESULT ')]
        check(done['windows'] == '1' and done['frames'] == '10' and done['rf'] == 'off', done)
        check(len(rows) == 1, lines)
        actual = verify_score(rows[0])
        f = fields(rows[0])
        check(35_000 <= int(f['capture_us']) <= 150_000 and int(f['infer_us']) > 0, f)
        dump, raw_lines = board.ask('dump', 'OK AI DUMP')
        raw = []
        for line in raw_lines:
            if not line.startswith('DATA AI RAW '):
                continue
            item = fields(line)
            check(int(item['frame']) == len(raw) // 128, line)
            check(len(item['raw']) == 384, line)
            raw += [int(item['raw'][i:i+3], 16) for i in range(0, 384, 3)]
        check(len(raw) == 1280 and min(raw) < max(raw) <= 4095, 'Bad raw ADC capture')
        check(crc(raw) == dump['crc'] == f['crc'], 'Raw CRC mismatch')
        expected = Oracle(args.model).predict(raw) if args.oracle else None
        error = compare(actual, expected) if expected else None
        snapshots.append({'raw': raw, 'scores': actual, 'desktop_scores': expected,
                          'error': error, 'timing': f})
    check(snapshots[0]['raw'] != snapshots[1]['raw'], 'ADC capture was repeated/stale')
    done, lines = board.ask(f'run {args.windows}', 'OK AI DONE', timeout=max(20, args.windows * 0.5))
    rows = [s for s in lines if s.startswith('DATA AI RESULT ')]
    check(int(done['windows']) == args.windows and len(rows) == args.windows, done)
    for index, row in enumerate(rows):
        check(int(fields(row)['window']) == index, 'Dropped or reordered prediction')
        verify_score(row)
    results['live'] = {'snapshots': snapshots, 'run': done,
                       'predictions_per_second': args.windows * 1000 / int(done['elapsed_ms']),
                       'inference_us': [int(fields(row)['infer_us']) for row in rows]}
    if args.windows >= 100:
        check(18 <= results['live']['predictions_per_second'] <= 21,
              'Continuous acquisition is not sustaining the model input rate')

def control(board, args, results):
    start, _ = board.ask('start', 'OK AI START')
    token = start['token']
    board.wait(lambda s: s.startswith('DATA AI RESULT ') and fields(s).get('token') == token)
    busy, _ = board.ask('reset', 'OK AI RESET', success=False)
    check(busy.get('token') is not None and board.lines[-1].startswith('FAIL AI BUSY'), 'Reset allowed while active')
    _, _ = board.ask('run 1', 'OK AI START', success=False)
    check(board.lines[-1].startswith('FAIL AI BUSY'), 'Second run accepted while active')
    now = time.monotonic()
    board.ask('stop', 'OK AI STOP')
    done = fields(board.wait(lambda s: s.startswith('OK AI DONE ') and fields(s).get('token') == token, 3))
    check(done['stopped'] == '1' and int(done['windows']) < 1200 and done['rf'] == 'off', done)
    elapsed = time.monotonic() - now
    check(elapsed < 1, 'Stop too slow')
    status, _ = board.ask('status', 'OK AI INFO')
    check(status['active'] == '0', status)
    results['control'] = {'stop_seconds': elapsed, 'run': done}

def parser(board, args, results):
    cases = [('run ' + n, 'ARGUMENT') for n in ['0', '-1', '+1', '1201', 'abc', '1junk', '9999999999999999999']]
    cases += [('unknown', 'COMMAND'), ('info extra', 'COMMAND')]
    for command, error in cases:
        board.ask(command, 'OK AI IMPOSSIBLE', success=False)
        check(board.lines[-1].startswith(f'FAIL AI {error} '), board.lines[-1])
    board.send('x' * 300)
    board.wait(lambda s: s == 'FAIL AI COMMAND_TOO_LONG')
    info(board, args, results)
    results['parser'] = {'rejected': len(cases) + 1, 'recovery': True}

def reset(board, args, results):
    board.ask('reset', 'OK AI RESET')
    board.ask('dump', 'OK AI DUMP', success=False)
    check(board.lines[-1].startswith('FAIL AI NO_CAPTURE'), 'Stale capture survived reset')
    data, _ = board.ask('run 1', 'OK AI DONE')
    check(data['windows'] == '1', data)
    results['reset'] = {'live_after_reset': True}

def main():
    parser_ = argparse.ArgumentParser(description=__doc__)
    parser_.add_argument('--port', default='/dev/ttyACM0')
    parser_.add_argument('--case', choices=['all', 'info', 'selftest', 'live', 'control', 'parser', 'reset'], default='all')
    parser_.add_argument('--windows', type=int, default=100)
    parser_.add_argument('--oracle', action='store_true', help='Requires numpy and ai-edge-litert; no silent skip')
    parser_.add_argument('--model', type=Path, default=Path(__file__).resolve().parent /
                         'build-cm55/022_Radar_AI_Gestures/model-generated/model.tflite')
    parser_.add_argument('--output', type=Path)
    args = parser_.parse_args()
    if not 1 <= args.windows <= 1200:
        parser_.error('--windows must be 1..1200')
    if args.oracle:
        Oracle(args.model)  # Fail before touching the board if dependencies/model missing.
    out = args.output or Path(__file__).resolve().parent / 'test-results' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    out.mkdir(parents=True, exist_ok=True)
    results = {'case': args.case, 'oracle_enabled': args.oracle, 'passed': False}
    with (out / 'radar-ai.log').open('w') as log:
        board = Board(args.port, log)
        try:
            board.ask('stop', 'OK AI STOP')
            # Stop ACK may precede DONE; poll until worker is idle.
            for _ in range(30):
                state, _ = board.ask('status', 'OK AI INFO')
                if state['active'] == '0': break
                time.sleep(0.1)
            check(state['active'] == '0', 'Board did not stop')
            tests = [info, selftest, live, control, parser, reset] if args.case == 'all' else [globals()[args.case]]
            for test in tests:
                test(board, args, results)
                print(f'PASS {test.__name__}', flush=True)
            results['passed'] = True
        except Exception as exc:
            results['error'] = repr(exc)
            raise
        finally:
            board.send('ai stop cleanup')
            board.serial.close()
            (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            print(f'Evidence: {out}', flush=True)
    print('PASS: hardware inference checks. Human gesture accuracy not assessed.')

if __name__ == '__main__':
    main()
