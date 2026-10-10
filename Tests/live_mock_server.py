"""A fake Ableton Live running the real FINISH Remote Script, for the end-to-end test of the plugin <-> Live protocol.

    python3 Tests/live_mock_server.py <home> <seconds> [live11]

Loads the demo set (see test_live_bridge_v3.py) into the mock Live object model, runs the FINISH bridge's
update_display() like Live does (~30 Hz), and stops when <home>/stop exists or after <seconds>.
Writes <home>/initial_state.json at start and <home>/final_state.json at the end (every clip of every track).
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_live_bridge_v3 as t  # noqa: E402


def state(song):
    return {tr.name: [[c.name, c.start_time, c.end_time, len(c._notes)] for c in tr.arrangement_clips] for tr in song.tracks}


def main():
    home, seconds = sys.argv[1], float(sys.argv[2])
    live12 = not (len(sys.argv) > 3 and sys.argv[3] == "live11")
    os.makedirs(home, exist_ok=True)
    wav = os.path.join(home, "hat.wav")
    t.make_wav(wav)
    song = t.demo_set(live12, wav)
    bridge, inbox = t.load_bridge(song, home)
    with open(os.path.join(home, "initial_state.json"), "w") as f:
        json.dump(state(song), f)
    print("mock Live ready, inbox:", inbox, flush=True)
    end = time.time() + seconds
    requests = 0
    while time.time() < end and not os.path.exists(os.path.join(home, "stop")):
        if os.path.exists(os.path.join(inbox, "request.json")):
            requests += 1
        bridge.update_display()
        time.sleep(0.03)
    with open(os.path.join(home, "final_state.json"), "w") as f:
        json.dump(state(song), f)
    print("mock Live stopped after %d request(s)" % requests, flush=True)


if __name__ == "__main__":
    main()
