# Using mpatbtools
Use the EXEs in the repository if you are on Windows otherwise compile the source code. atbdump will dump the ATB into an XML containing information about the sprites in an ATB and a TPL contatining the image for the sprites to use. atbpack will generate an ATB file from an XML and TPL file which can be used directly in the game.


#Compiling
This program requires the mxml source code to compile with config.h.in renamed to config.h. After that, it can be compiled readily. If you want to compile on Windows, add _CRT_SECURE_NO_WARNINGS to your preprocessor definitions.
