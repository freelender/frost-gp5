"""Global settings menu navigation for host tests, with real PARA gestures
(turn/press) through the 32k menu tree: GLOBAL SETTINGS -> AUDIO (INPUT GAIN,
OUTPUT LEVEL, OUTPUT MODE, CAB OVERRIDE, GLOBAL EQ -> BASS/MIDDLE/TREBLE),
DISPLAY (HOME SCREEN, BRIGHTNESS, SETLIST HOLD), TAP TEMPO (TAP TEMPO, TAP
NOTE, TAP TARGET, TEMPO LIGHT), SYSTEM (FIRMWARE, VERSION, RESET SETTINGS, FACTORY
RESET). Row numbers are the firmware's SET_* rows."""
IN_GAIN,OUT_LEVEL,OUT_MODE,CAB,BRIGHT,HOME,SETLIST_HOLD,VERSION,RESET,FACTORY,EQ_BASS,EQ_MIDDLE,EQ_TREBLE,TAP_NOTE,TEMPO_LIGHT,TAP_TEMPO,TAP_TARGET,BACK=range(18)
FOOTSWITCH=18
FIRMWARE=19
DIRECT_MONITOR=20
BLUETOOTH=21
TOP_AUDIO,TOP_DISPLAY,TOP_TAP,TOP_SYSTEM,TOP_BACK=range(5)
AUDIO_EQ=4
PATH={BLUETOOTH:(TOP_SYSTEM,None,2),DIRECT_MONITOR:(TOP_AUDIO,None,5),IN_GAIN:(TOP_AUDIO,None,0),OUT_LEVEL:(TOP_AUDIO,None,1),OUT_MODE:(TOP_AUDIO,None,2),CAB:(TOP_AUDIO,None,3),
      EQ_BASS:(TOP_AUDIO,AUDIO_EQ,0),EQ_MIDDLE:(TOP_AUDIO,AUDIO_EQ,1),EQ_TREBLE:(TOP_AUDIO,AUDIO_EQ,2),
      HOME:(TOP_DISPLAY,None,0),BRIGHT:(TOP_DISPLAY,None,1),SETLIST_HOLD:(TOP_DISPLAY,None,2),FOOTSWITCH:(TOP_DISPLAY,None,3),
      TAP_TEMPO:(TOP_TAP,None,0),TAP_NOTE:(TOP_TAP,None,1),TAP_TARGET:(TOP_TAP,None,2),TEMPO_LIGHT:(TOP_TAP,None,3),
      FIRMWARE:(TOP_SYSTEM,None,0),VERSION:(TOP_SYSTEM,None,1),RESET:(TOP_SYSTEM,None,2),FACTORY:(TOP_SYSTEM,None,3)}
def turn_to(c,pos):c.ge_turn(pos-c.ge_settings_pos())
def top(c):
    """Back up to the GLOBAL SETTINGS list through each submenu's BACK row."""
    while c.ge_settings_menu():turn_to(c,c.ge_settings_count()-1);c.ge_press()
def go_row(c,row):
    top(c)
    if row==BACK:turn_to(c,TOP_BACK);return
    first,sub,pos=PATH[row];turn_to(c,first);c.ge_press()
    if sub is not None:turn_to(c,sub);c.ge_press()
    if row in (RESET,FACTORY) and hasattr(c,"ge_bluetooth_now"):pos+=1
    turn_to(c,pos)
    assert c.ge_settings_row()==row,(row,c.ge_settings_row())
def leave(c):
    """Save and leave from anywhere in the settings list."""
    go_row(c,BACK);c.ge_press()
def set_value(c,row,turns):
    go_row(c,row);c.ge_press();c.ge_turn(turns);c.ge_press()
