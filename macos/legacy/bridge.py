#!/usr/bin/env python3
"""Send real PC metrics to SpaceSmart over BLE or USB. Does not infer AI activity.

An optional status directory accepts fresh, explicitly observed agent state from
an external adapter. No app hooks, global settings or background jobs are installed.
"""
import argparse
import asyncio
import datetime
import json
import math
from pathlib import Path
import sys
import time

SERVICE_UUID = 'b72c0001-6e35-4b4f-8ad8-08d90a5c9170'
RX_UUID = 'b72c0002-6e35-4b4f-8ad8-08d90a5c9170'
STATES = {'unknown', 'idle', 'busy', 'waiting', 'complete', 'error', 'interrupted', 'offline'}


def clean_title(value):
    text = ''.join(c for c in str(value) if c.isprintable()).replace('|', ' ')
    return text.encode('utf-8')[:63].decode('utf-8', errors='ignore')


def agent_line(app, directory, now):
    unknown = f'agent {app} unknown 0 |\n'
    if directory is None:
        return unknown
    try:
        path = directory / f'{app}.json'
        with path.open('rb') as handle:
            content = handle.read(8193)
        if len(content) > 8192:
            return unknown
        record = json.loads(content)
        updated = record['updated_at']
        elapsed = record.get('elapsed_seconds', 0)
        state = record['state']
        if not isinstance(updated, (int, float)) or not math.isfinite(updated):
            return unknown
        age = now - updated
        if age < -3 or age > 12 or state not in STATES:
            return unknown
        if type(elapsed) is not int or not 0 <= elapsed <= 604788:
            return unknown
        if state == 'busy':
            elapsed += int(max(age, 0))
        title = clean_title(record.get('title', ''))
        return f'agent {app} {state} {elapsed} |{title}\n'
    except (OSError, ValueError, KeyError, TypeError):
        return unknown


class Metrics:
    def __init__(self, psutil):
        self.psutil = psutil
        psutil.cpu_percent(None)
        self.last_net = psutil.net_io_counters()
        self.last_at = time.monotonic()

    def lines(self, directory):
        at = time.monotonic()
        network = self.psutil.net_io_counters()
        dt = max(at - self.last_at, .001)
        rx = max(0, network.bytes_recv - self.last_net.bytes_recv) / dt / 1024
        tx = max(0, network.bytes_sent - self.last_net.bytes_sent) / dt / 1024
        self.last_net, self.last_at = network, at
        cpu = self.psutil.cpu_percent(None)
        memory = self.psutil.virtual_memory().percent
        clock = datetime.datetime.now()
        now = time.time()
        return [f'pc {cpu:.1f} {memory:.1f} {rx:.1f} {tx:.1f}\n',
                f'clock {clock.hour} {clock.minute} {clock.second}\n',
                agent_line('claude', directory, now), agent_line('codex', directory, now)]


async def send_metrics(send, psutil, args):
    metrics = Metrics(psutil)
    await asyncio.sleep(.3)  # psutil's first CPU reading needs a sampling interval.
    while True:
        started = time.monotonic()
        for line in metrics.lines(args.status_dir):
            payload = line.encode('utf-8')
            if len(payload) > 96:
                raise ValueError('telemetry record exceeds the firmware line buffer')
            await send(payload)
        await asyncio.sleep(max(.2, 2 - (time.monotonic() - started)))


async def ble_session(psutil, args):
    from bleak import BleakClient, BleakScanner
    found = await BleakScanner.discover(timeout=6, return_adv=True, service_uuids=[SERVICE_UUID])
    candidates = [device for device, ad in found.values()
                  if (args.device and device.address.lower() == args.device.lower()) or
                  (not args.device and SERVICE_UUID in [s.lower() for s in ad.service_uuids])]
    if len(candidates) != 1:
        addresses = ', '.join(d.address for d in candidates)
        raise RuntimeError(f'Expected one SpaceSmart, found {len(candidates)}. Use --device ADDRESS. {addresses}')
    # CoreBluetooth pairs on access to the encrypted characteristic on macOS.
    async with BleakClient(candidates[0], timeout=30, pair=sys.platform != 'darwin') as client:
        print(f'Connected to SpaceSmart ({candidates[0].address})', flush=True)
        async def send(payload):
            for offset in range(0, len(payload), 20):
                await client.write_gatt_char(RX_UUID, payload[offset:offset+20], response=True)
        await send_metrics(send, psutil, args)


async def serial_session(psutil, args):
    import serial
    with serial.Serial(args.serial, 115200, timeout=0, write_timeout=1) as port:
        print(f'Connected to {args.serial}', flush=True)
        await asyncio.sleep(2)
        async def send(payload):
            # Drain routine sensor JSON replies so the OS receive buffer stays bounded.
            port.read(min(port.in_waiting, 8192))
            port.write(payload)
        await send_metrics(send, psutil, args)


async def run(args):
    import psutil
    while True:
        try:
            await (serial_session(psutil, args) if args.serial else ble_session(psutil, args))
        except (OSError, RuntimeError, TimeoutError) as exc:
            print(f'Connection unavailable: {exc}; retrying in 5 seconds', file=sys.stderr, flush=True)
            await asyncio.sleep(5)
        except Exception as exc:
            # Bleak exposes platform-specific exception classes; retain reconnection.
            if type(exc).__module__.startswith(('bleak', 'dbus_fast')):
                print(f'BLE disconnected: {exc}; retrying in 5 seconds', file=sys.stderr, flush=True)
                await asyncio.sleep(5)
            else:
                raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument('--ble', action='store_true')
    transport.add_argument('--serial', metavar='PORT')
    parser.add_argument('--device', help='Optional BLE address/UUID if multiple devices are present')
    parser.add_argument('--status-dir', type=Path, help='Optional directory containing fresh claude.json/codex.json')
    args = parser.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass
    except ImportError as exc:
        parser.exit(2, f'Missing dependency: {exc}. Install companion/requirements.txt in a virtual environment.\n')


if __name__ == '__main__':
    main()
