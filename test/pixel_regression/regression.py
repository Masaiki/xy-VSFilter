"""Generate native-renderer jobs and compare exact per-frame SHA-256 results."""
import argparse
import csv
import itertools
import json
import os
import re
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


HEADER = """[Script Info]
ScriptType: v4.00+
PlayResX: 320
PlayResY: 240
ScaledBorderAndShadow: yes
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,32,&H00FFFFFF,&H0000FFFF,&H00302010,&H00605040,0,0,0,0,100,100,0,0,1,2,2,7,0,0,0,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""
GRADIENT = r"\1vc(&H0000FF&,&H00FF00&,&HFF0000&,&HFFFFFF&)"


def timestamp(value):
    h, m, s = value.split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def read_ass(path):
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "utf-16", "cp932", "gb18030", "cp1252"):
        try:
            return data.decode(encoding)
        except UnicodeError:
            pass
    raise ValueError(f"Cannot decode {path}")


def event_times(path):
    times = set()
    events = 0
    for row in read_ass(path).splitlines():
        if not row.lower().startswith("dialogue:"):
            continue
        fields = next(csv.reader([row.split(":", 1)[1]]))
        try:
            start, end = map(timestamp, fields[1:3])
        except (ValueError, IndexError):
            continue
        if end <= start:
            continue
        events += 1
        times.update((start, round((start + end) / 2, 6), round(max(start, end - .01), 6), end))
    return sorted(times), events


def write_jobs(path, jobs):
    with path.open("w", encoding="utf-8", newline="") as stream:
        for job in jobs:
            stream.write(f"{job['id']}\t{job['path']}\t"
                         + " ".join(map(str, job['config'])) + "\t"
                         + " ".join(map(str, job['times'])) + "\n")


def generate(args):
    out = args.output.resolve()
    fixtures = out / "fixtures"
    fixtures.mkdir(parents=True, exist_ok=True)
    from PIL import Image
    texture = Image.new("RGBA", (16, 16))
    texture.putdata([(x * 17, y * 17, (x ^ y) * 17, (x + y) * 8)
                     for y in range(16) for x in range(16)])
    texture.save(fixtures / "texture.png")
    cases = {
        "plain": "", "fractional": r"\pos(30.375,40.625)",
        "rotation": r"\frz27\frx15\fry-20", "scale": r"\fscx125\fscy75\fsp1.5",
        "shear": r"\fax.25\fay-.12", "opaque_box": r"\bord4",
        "be": r"\be3", "fractional_be": r"\be1.5", "blur": r"\blur2.3",
        "be_blur": r"\blur1.7\be2", "thin_blur": r"\bord0\blur.4",
        "asymmetric_border": r"\xbord5\ybord1\xshad-3\yshad4",
        "fade": r"\fad(400,500)\blur3", "karaoke": r"\kf150",
        "gradient": GRADIENT, "gradient_blur": GRADIENT + r"\blur2.7",
        "gradient_be": GRADIENT + r"\be3", "gradient_be_blur": GRADIENT + r"\be2\blur1.8",
        "gradient_alpha": GRADIENT + r"\1va(&H00&,&H80&,&HFF&,&H20&)",
        "gradient_outline": r"\3vc(&H0000FF&,&H00FF00&,&HFF0000&,&HFFFFFF&)\bord5\blur2",
        "gradient_shadow": r"\4vc(&H0000FF&,&H00FF00&,&HFF0000&,&HFFFFFF&)\shad5\blur2",
        "gradient_clip": GRADIENT + r"\clip(m 0 0 l 320 0 320 240 0 240)",
        "gradient_iclip": GRADIENT + r"\iclip(m 45 50 l 90 50 90 70 45 70)",
        "gradient_rect": GRADIENT + r"\clip(45,45,180,66)",
        "gradient_moving_clip": GRADIENT + r"\clip(m 0 0 l 200 0 200 200 0 200)\movevc(0,0,40,20)",
        "gradient_fade": GRADIENT + r"\fad(400,600)\blur3",
        "gradient_karaoke": GRADIENT + r"\2vc(&HFFFFFF&,&H000000&,&H00FF00&,&HFF0000&)\kf150",
        "image": r"\1img(texture.png)", "image_blur": r"\1img(texture.png)\blur2",
        "image_clip": r"\1img(texture.png)\clip(m 0 0 l 320 0 320 240 0 240)",
        "image_offset": r"\1img(texture.png,3,5)\fad(300,300)",
        "image_animation": r"\1img(texture.png)\t(0,2000,\1img(texture.png,20,30))",
        "distort": r"\distort(1.2,.1,.8,1.1,-.2,.9)",
        "random": r"\rnds1234\rnd3", "z": r"\z30\frx20",
        "symbol_rotation": r"\frs20", "vertical_spacing": r"\fsvp8",
        "jitter": r"\jitter(2,3,2,3,100,42)", "mover": r"\mover(80,80,140,100,0,180,20,40)",
        "moves3": r"\moves3(30,40,100,100,150,50)",
        "moves4": r"\moves4(30,40,50,100,120,120,180,50)",
        "moving_org": r"\org(100,100,140,120)\frz30", "animated": r"\t(0,2000,\frz30\blur3\fscx120)",
        "overlap": GRADIENT + r"\blur2", "polygon": GRADIENT + r"\blur1.5",
    }
    paths = []
    for name, tags in cases.items():
        head = HEADER.replace(",1,2,2,7,", ",3,2,2,7,") if name == "opaque_box" else HEADER
        body = "Pixel test AV gy" if name != "polygon" else r"{\p1}m 0 0 l 130 0 130 50 0 50"
        position = "" if name in ("mover", "moves3", "moves4") else r"\pos(30.375,40.625)"
        content = head + f"Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,{{{position}{tags}}}{body}\n"
        if name == "overlap":
            content += r"Dialogue: 1,0:00:00.00,0:00:02.00,Default,,0,0,0,,{\pos(50.625,50.375)\alpha&H80&\blur3}OVERLAP" + "\n"
        path = fixtures / f"{name}.ass"
        path.write_text(content, encoding="utf-8-sig")
        paths.append((name, path))
    jobs = []

    def add(group, path, config, times):
        jobs.append(dict(id=f"j{len(jobs):05d}", group=group, path=str(path.resolve()), config=config, times=times))

    times = [0, .01, .333, .5, 1, 1.999, 2, .5, .5, .333, 0]
    for (name, path), mode, level, surface, size in itertools.product(
            paths, range(2), range(5), range(3), [(320, 240), (640, 360)]):
        add(f"synthetic/{name}", path, [*size, mode, level, surface, (mode + level) % 2], times)
    inventory = {}
    for path in sorted(args.repo.glob("test/*.ass")):
        samples, events = event_times(path)
        inventory[path.name] = dict(events=events, samples=len(samples))
        # Cover every dialogue without expanding identical overlapping event times.
        for mode, surface in itertools.product(range(2), range(3)):
            add(f"corpus/{path.name}", path, [640, 360, mode, 3, surface, 0], samples + samples[:3])
    if args.subtitle:
        samples, events = event_times(args.subtitle)
        text = read_ass(args.subtitle)
        inventory[args.subtitle.name] = dict(events=events, samples=len(samples),
            fonts=sorted(set(re.findall(r"^Style:[^,]*,([^,]*)", text, re.M))),
            images=sorted(set(re.findall(r"\\[1-4]img\(([^,)]+)", text))))
        for mode, level, surface in itertools.product(range(2), [3, 4], range(3)):
            add("user/event_boundaries", args.subtitle, [1920, 1080, mode, level, surface, 0], samples + samples[:5])
    write_jobs(out / "jobs.tsv", jobs)
    (out / "jobs.json").write_text(json.dumps(jobs, indent=2), encoding="utf-8")
    (out / "inventory.json").write_text(json.dumps(inventory, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(dict(jobs=len(jobs), frames=sum(len(j['times']) for j in jobs), inventory=inventory), ensure_ascii=False))


def results(path):
    parsed = {}
    for line in path.read_text().splitlines():
        fields = line.split("\t")
        if len(fields) != 5:
            raise ValueError(f"Invalid render result: {line}")
        parsed[fields[0], int(fields[1])] = fields[2:]
    return parsed


def compare(args):
    out = args.output
    jobs = {j['id']: j for j in json.loads((out / 'jobs.json').read_text())}
    expected = {(j['id'], i) for j in jobs.values() for i in range(len(j['times']))}
    before, after = results(args.before), results(args.after)
    before = {k: v for k, v in before.items() if k in expected}
    after = {k: v for k, v in after.items() if k in expected}
    mismatch = []
    missing = []
    for key in sorted(expected):
        absent = key not in before or key not in after
        different = not absent and (before[key][1] != after[key][1]
            or (not args.reference and before[key][0] != after[key][0]))
        if absent or different:
            job = jobs[key[0]]
            (missing if absent else mismatch).append(dict(job=key[0], frame=key[1], group=job['group'], config=job['config'],
                                 time=job['times'][key[1]], before=before.get(key), after=after.get(key)))
    summary = dict(expected_frames=len(expected), baseline_frames=len(before), candidate_frames=len(after),
        compared_frames=len(expected) - len(missing), missing_frames=len(missing),
        mismatched_frames=len(mismatch), groups=dict(Counter(m['group'] for m in mismatch)),
        candidate_nonblank=sum(v[2] == '1' for v in after.values()),
        render_errors=sum(int(v[0]) < 0 for v in after.values()))
    (out / 'comparison.json').write_text(json.dumps(dict(summary=summary, mismatches=mismatch, missing=missing), indent=2))
    failed_ids = {m['job'] for m in mismatch + missing}
    write_jobs(out / 'failed.tsv', [j for j in jobs.values() if j['id'] in failed_ids])
    print(json.dumps(summary, indent=2))
    return 1 if missing or mismatch or summary['render_errors'] else 0


def run(args):
    jobs = json.loads((args.output / 'jobs.json').read_text())
    destination = args.after
    failures = []
    started = time.monotonic()
    destination.write_text('')
    # Restart after a native failure so one existing crashing case cannot hide
    # coverage of the remaining scripts. Never count a failed case as a match.
    with destination.open('a') as output:
        while jobs:
            chunk = jobs[:100]
            manifest = destination.with_suffix('.jobs.tsv')
            partial = destination.with_suffix('.partial.tsv')
            log = destination.with_suffix('.log')
            write_jobs(manifest, chunk)
            with log.open('w') as diagnostics:
                try:
                    command = [str(args.runner), str(manifest), str(partial)]
                    if args.reference:
                        command += ['-', str(args.reference)]
                    result = subprocess.run(command,
                        stdout=diagnostics, stderr=subprocess.STDOUT, timeout=1200)
                    code = result.returncode
                except subprocess.TimeoutExpired:
                    code = 'timeout'
            data = partial.read_text() if partial.exists() else ''
            output.write(data)
            output.flush()
            counts = Counter(row.split('\t')[0] for row in data.splitlines())
            complete = {j['id'] for j in chunk if counts[j['id']] == len(j['times'])}
            if code:
                begins = re.findall(r'BEGIN (j\d+)', log.read_text(errors='replace'))
                failed = begins[-1] if begins else chunk[0]['id']
                failures.append(dict(job=failed, returncode=code))
                complete.add(failed)
            elif len(complete) != len(chunk):
                raise RuntimeError('Runner returned success with incomplete output')
            jobs = [j for j in jobs if j['id'] not in complete]
            print(f'{destination.name}: remaining_jobs={len(jobs)} failures={len(failures)} elapsed={time.monotonic()-started:.1f}s', flush=True)
    destination.with_suffix('.failures.json').write_text(json.dumps(failures, indent=2))


def resume(args):
    jobs = json.loads((args.output / 'jobs.json').read_text())
    destination = args.after
    saved = results(destination) if destination.exists() else {}
    # Salvage only complete records from an interrupted native process.
    partial = destination.with_suffix('.partial.tsv')
    if partial.exists():
        for line in partial.read_text().splitlines():
            fields = line.split('\t')
            if len(fields) == 5 and len(fields[3]) == 64 and fields[4] in ('0', '1'):
                saved[fields[0], int(fields[1])] = fields[2:]
    counts = Counter(k[0] for k in saved)
    complete = {j['id'] for j in jobs if counts[j['id']] == len(j['times'])}
    with destination.open('w') as stream:
        for (job, frame), values in sorted(saved.items()):
            if job in complete:
                stream.write('\t'.join([job, str(frame), *values]) + '\n')
    pending = [j for j in jobs if j['id'] not in complete]
    work = destination.parent / (destination.stem + '-jobs')
    work.mkdir(exist_ok=True)
    failures = []

    def execute(job):
        manifest = work / (job['id'] + '.jobs.tsv')
        result_path = work / (job['id'] + '.tsv')
        write_jobs(manifest, [job])
        command = [str(args.runner), str(manifest), str(result_path)]
        if args.reference:
            command += ['-', str(args.reference)]
        with (work / (job['id'] + '.log')).open('w') as log:
            try:
                code = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                      timeout=args.timeout).returncode
            except subprocess.TimeoutExpired:
                code = 'timeout'
        data = result_path.read_text() if result_path.exists() else ''
        lines = [line for line in data.splitlines() if len(line.split('\t')) == 5]
        if not code and len(lines) != len(job['times']):
            code = 'incomplete output'
        return job, code, '\n'.join(lines) + ('\n' if lines else '')

    print(f'Resuming {len(pending)} jobs; {len(complete)} already complete', flush=True)
    with ThreadPoolExecutor(max_workers=args.workers) as pool, destination.open('a') as output:
        futures = [pool.submit(execute, job) for job in pending]
        for future in as_completed(futures):
            job, code, data = future.result()
            output.write(data)
            output.flush()
            if code:
                failures.append(dict(job=job['id'], returncode=code))
            print(f"{job['id']} {job['group']} exit={code}", flush=True)
            destination.with_suffix('.failures.json').write_text(json.dumps(failures, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['generate', 'compare', 'run', 'resume'])
    parser.add_argument('--output', type=Path, default=Path('bin/pixel-regression'))
    parser.add_argument('--repo', type=Path, default=Path('.'))
    parser.add_argument('--subtitle', type=Path)
    parser.add_argument('--before', type=Path)
    parser.add_argument('--after', type=Path)
    parser.add_argument('--runner', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--workers', type=int, default=6)
    parser.add_argument('--timeout', type=int, default=7200)
    parser.add_argument('--font-dir', type=Path)
    args = parser.parse_args()
    if args.font_dir:
        jobs = json.loads((args.output / 'jobs.json').read_text())
        sources = {j['path'] for j in jobs if j['group'].startswith('user/')}
        if len(sources) != 1:
            parser.error('--font-dir requires exactly one user subtitle source')
        os.environ['PIXEL_TEST_FONT_DIR'] = str(args.font_dir.resolve())
        os.environ['PIXEL_TEST_FONT_SOURCE'] = sources.pop()
    sys.exit(dict(generate=generate, compare=compare, run=run, resume=resume)[args.action](args))
