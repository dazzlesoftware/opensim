YOption allowall;
integer[] input; integer[] alias; object wrapped; object nil;
integer h0; integer h1; integer h2; integer h3; integer h4; integer h5; integer left; integer after;
default { state_entry() {
    h0=xmrHeapUsed(); input=new integer[](3); h1=xmrHeapUsed();
    alias=input; h2=xmrHeapUsed(); wrapped=input; h3=xmrHeapUsed();
    input.len0=1; h4=xmrHeapUsed(); llSleep(0.1);
    h5=xmrHeapUsed(); left=xmrHeapLeft();
    input=(integer[])nil; alias=(integer[])nil; wrapped=nil; after=1;
} }
