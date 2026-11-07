# The presentation video

`make_video.sh` rebuilds the video of about 2:32 from the simulator alone: no camera, no narration, no download. `plan.py` writes the 16
scenarios (`tools/video/scenarios/video_*.txt`) and a timeline, `formant.py` synthesises the modulator voice ("o three vo co der", "ro bot") with numpy,
the simulator renders each segment's screens and audio, `beat.py` joins the segments and mixes an original four-on-the-floor beat under them,
and `compose.py` builds the 1920x1080 frames (skin colours, caption strip) and encodes the three mp4 files with ffmpeg.

Needs Python 3 with numpy and Pillow, ffmpeg, and the host build. Output goes to `~/Videos/o3-voc03er/` (`--out`), scratch to `$TMPDIR/o3-voc03er-video` (`--work`);
nothing generated is meant for git. The picture is the simulator, not the unit.
