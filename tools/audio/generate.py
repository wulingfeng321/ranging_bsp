"""Generate STANDARD, POSITION, sine and regression WAVs with one consistent CLI."""

import argparse
import math
from pathlib import Path

from waveforms import (
    GENERATED_DIR, audio_name, chirp_group, position_blocks,
    repeat_blocks, sine_blocks, write_wav,
)


def finite_seconds(value):
    value = float(value)
    if not math.isfinite(value) or value < 0:
        raise argparse.ArgumentTypeError("must be finite and >= 0")
    return value


def positive_seconds(value):
    value = finite_seconds(value)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be > 0")
    return value


def positive_count(value):
    value = int(value)
    if value < 1:
        raise argparse.ArgumentTypeError("must be >= 1")
    return value


def amplitude_value(value):
    value = finite_seconds(value)
    if value > 1:
        raise argparse.ArgumentTypeError("must be in 0..1")
    return value


def output_options(parser, single=True):
    parser.add_argument("--output-dir", type=Path, default=GENERATED_DIR,
                        help="output folder (default: beside this script, generated/)")
    if single:
        parser.add_argument("--output", type=Path, help="explicit WAV path; overrides --output-dir")


def create_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="kind", required=True)
    for kind, default_amplitude in (("standard", 0.9), ("wide", 0.75), ("position", 0.65), ("sine", 0.8)):
        command = commands.add_parser(kind, help=f"generate {kind} audio")
        output_options(command)
        command.add_argument("--sample-rate", type=int,
                             choices=(48000,), default=48000)
        command.add_argument("--amplitude", type=amplitude_value, default=default_amplitude,
                             help="fraction of PCM full scale, 0..1")
        if kind in ("standard", "wide"):
            command.add_argument("--rounds", type=positive_count, default=3)
            command.add_argument("--signatures", type=positive_count, default=5, help="signatures per group")
            command.add_argument("--interval", type=positive_seconds, default=0.5, help="signature interval, seconds")
            command.add_argument("--gap-seconds", type=finite_seconds, default=1.5, help="extra silence between groups")
            command.add_argument("--tail-seconds", type=finite_seconds, default=5.0)
            command.add_argument("--group-only", action="store_true", help="one group; no extra gaps or tail")
        else:
            command.add_argument("--duration", type=positive_seconds, default=30.0 if kind == "position" else 60.0)
            if kind == "position":
                command.add_argument("--interval", type=positive_seconds, default=0.5)
            else:
                command.add_argument("--frequency", type=positive_seconds, default=500.0, help="sine frequency, Hz")
    all_audio = commands.add_parser("all", help="generate the three current 48 kHz playback files")
    output_options(all_audio, single=False)
    all_audio.add_argument("--regression", action="store_true", help="also generate two 48 kHz standard/WIDE regression inputs")
    return parser


def generate_one(args):
    rate = args.sample_rate
    if args.kind in ("standard", "wide"):
        group = chirp_group(args.kind, rate, args.amplitude, args.signatures, args.interval)
        if args.group_only:
            kind, frames, blocks = f"{args.kind}_group", len(group), [group]
        else:
            frames = (len(group) * args.rounds + round(rate * args.gap_seconds) * (args.rounds - 1)
                      + round(rate * args.tail_seconds))
            kind = f"{args.kind}_repeat"
            blocks = repeat_blocks(group, rate, args.rounds, args.gap_seconds, args.tail_seconds)
    else:
        frames = round(rate * args.duration)
        if frames < 1:
            raise ValueError("duration must contain at least one sample")
        kind = args.kind
        if kind == "position":
            if round(rate * args.interval) < round(rate * 0.012):
                raise ValueError("interval must be at least 0.012 seconds")
            blocks = position_blocks(rate, frames, args.amplitude, args.interval)
        else:
            if args.frequency >= rate / 2:
                raise ValueError("frequency must be below half the sample rate")
            blocks = sine_blocks(rate, frames, args.frequency, args.amplitude)
    filename = audio_name(kind, rate, frames, getattr(args, "frequency", 500))
    path = (args.output or args.output_dir / filename).resolve()
    if path.suffix.lower() != ".wav":
        raise ValueError("output must have a .wav extension")
    count = write_wav(path, rate, blocks)
    if count != frames:
        raise ValueError(f"frame count mismatch: expected {frames}, wrote {count}")
    print(f"Saved: {path}\nFormat: {rate} Hz / mono / PCM16; {count / rate:g} s; {count} frames")
    return path


def main(argv=None):
    parser = create_parser()
    args = parser.parse_args(argv)
    try:
        if args.kind != "all":
            generate_one(args)
            return
        presets = [["standard"], ["position"], ["sine"]]
        if args.regression:
            presets += [["standard", "--group-only"], ["wide"]]
        for preset in presets:
            generate_one(parser.parse_args(preset + ["--output-dir", str(args.output_dir)]))
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
