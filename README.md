# Simple software cursor renderer for screen recording

Zero copy screen recording (like kmsgrab in ffmpeg) dont't records cursor. This
software renders cursor image over real cursor. This allows to record video with
cursor.

# Compile

```
$ make
```

# Run

```
$ ./softwarecursor-x11 [-s maximum-cursor-size] [-r highlight-radius]
```

Default maximum cursor size is 32. Default highlight radius is same as cursor
size. Highlighting can be disabled with `-r 0` argument.


# Dependencies

This software uses pure X11 with extensions: Xfixes, XInput2 ad Xext.

# Screen recording using ffmpeg

For kmsgrab source is capability `cap_sys_admin+ep` required. Run following
command to add capabilities.

```
# setcap cap_sys_admin+ep `which ffmpeg`
```

To record desktop using ffmpeg run:

```
ffmpeg \
  -v verbose \
  -f pulse -i default \
  -framerate 60 \
  -thread_queue_size 128 \
  -device /dev/dri/card0 \
  -f kmsgrab -i - \
  -af "asetpts=N/SR/TB" \
  -vf 'hwmap=derive_device=vaapi,scale_vaapi=format=nv12' \
  -c:a aac \
  -c:v h264_vaapi \
  video.mkv
```

# Screenshots

![Screenshot](https://raw.githubusercontent.com/wiki/mireq/software-cursor/softwarecursor.gif)
