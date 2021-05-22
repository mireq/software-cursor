softwarecursor-x11: softwarecursor-x11.c
	${CC} $< -o $@ -lX11 -lXfixes -lXi -lXext
