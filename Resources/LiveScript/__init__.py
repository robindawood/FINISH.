# FINISH bridge for Ableton Live.
# Installed automatically by the FINISH plugin into "User Library/Remote Scripts/FINISH".
# Select "FINISH" once in Live > Settings > Link, Tempo & MIDI > Control Surface (no MIDI ports needed).
from .finish_bridge import FinishBridge


def create_instance(c_instance):
    return FinishBridge(c_instance)
