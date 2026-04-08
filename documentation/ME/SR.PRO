;***************************************************************
;* Funktion serread: einlesen von bildserien                   *
;***************************************************************
;* Aufruf :   volume=serread(name,px_width)
;* Parameter: name =      Filename mit Bilddaten (PC/Leutron)
;*            px_width =  Bildformat
;*
;* Bsp:       images=serread('../scratch/iv3.im',256)
;***************************************************************


function sr,name,px_width

nix=bytarr(140)
imgx=px_width
imgy=imgx
volume1 = Bytarr(imgx, imgy)
;*********************************Read IVUS***********
Openr, 1, name
status=fstat(1)
imgz= status.SIZE/imgx
;imgz=3342336/256  ** nur 51 Bilder
imgz=imgz/imgx
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
volume=bytarr(imgx,imgy,imgz)
for i=0,imgz-1 do begin
   readu,1,nix
   Readu, 1, volume1
   volume(*,*,i)=volume1
endfor
Close, 1
return,volume
end