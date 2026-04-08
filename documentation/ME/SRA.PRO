;***************************************************************
;* Funktion serread: einlesen von bildserien                   *
;***************************************************************
;* Aufruf :   volume=serread(name,px_width)
;* Parameter: name =      Filename mit Bilddaten (PC/Leutron)
;*            px_width =  Bildformat
;*
;* Bsp:       images=serreada('../scratch/iv3.im',256)
;***************************************************************


function sra,name,px_width

imgx=px_width
imgy=imgx
volume1 = Bytarr(imgx, imgy)
;*********************************Read IVUS***********
Openr, 1, name
status=fstat(1)
imgz= status.SIZE/256
;imgz=3342336/256
imgz=imgz/256
ltext,'Images in Series',string(imgz),id
imz=imgz
if imgz gt 10 then begin
        nname=texti('Enter Number of Images:','')
        textweg,id
        nname=nname(0)
        imz=fix(nname)
endif
if imz lt imgz then imgz=imz
if imgz gt 185  then imgz=185


volume=bytarr(imgx,imgy,imgz,/nozero)

Readu, 1, volume

close,1
return,volume
end
