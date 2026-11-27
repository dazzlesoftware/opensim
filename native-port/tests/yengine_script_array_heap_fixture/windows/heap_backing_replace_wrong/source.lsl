YOption allowall;
integer[] input; object raw;
integer h0; integer h1; integer h2; integer h3; integer h4; integer h5; integer h6; integer after; integer failure;
default { state_entry() {
    h0=xmrHeapUsed(); input=new integer[](3); h1=xmrHeapUsed();
    raw=xmrFixedArrayAllocI(5); h2=xmrHeapUsed();
    input.obj=raw; h3=xmrHeapUsed(); input.obj=input.obj; h4=xmrHeapUsed();
    input.obj="wrong"; h5=xmrHeapUsed(); llSleep(0.1);
    h6=xmrHeapUsed(); after=1; failure=input[0];
} }
