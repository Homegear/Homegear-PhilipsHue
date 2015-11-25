AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS = -Wall -std=c++11 -DFORTIFY_SOURCE=2 -DGCRYPT_NO_DEPRECATED
AM_LDFLAGS = -Wl,-rpath=/lib/homegear -Wl,-rpath=/usr/lib/homegear -Wl,-rpath=/usr/local/lib/homegear

libdir = $(localstatedir)/lib/homegear/modules
lib_LTLIBRARIES = mod_philipshue.la
mod_philipshue_la_SOURCES = PhilipsHue.cpp Factory.cpp GD.h PhilipsHueDeviceTypes.h PhilipsHuePeer.h PhilipsHuePacket.cpp PhilipsHuePacket.h PhilipsHue.h GD.cpp PhilipsHuePeer.cpp Factory.h PhysicalInterfaces/HueBridge.h PhysicalInterfaces/HueBridge.cpp PhysicalInterfaces/IPhilipsHueInterface.h PhysicalInterfaces/IPhilipsHueInterface.cpp PhilipsHueCentral.cpp PhilipsHueCentral.h PacketManager.h PacketManager.cpp Interfaces.h Interfaces.cpp
mod_philipshue_la_LDFLAGS =-module -avoid-version -shared
install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/mod_philipshue.la
