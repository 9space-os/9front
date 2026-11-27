# The firmware on the CD (/lib/firmware): linux-firmware's files under the
# names the drivers read (sys/src/build/host/guest/stage-firmware.py; firmware.list is
# made from this by stage-firmware.py -u, with each file's sha256).
#
#	SOURCE		a file or a glob in linux-firmware (FWDIR, default
#			/usr/lib/firmware; .zst, .xz or plain), or a licence
#			(also looked for in /usr/share/licenses/linux-firmware*)
#	TARGET		the name under /lib/firmware; ending in /: a
#			directory, the file keeps its name
#
# Firmware is a file of its own on the CD, not in the tree (CLAUDE.md:
# licences); each vendor's licence goes along.

# Intel WLAN, etheriwl (OpenBSD's names: iwn-* for the older, iwm-* for
# the 7000 family and newer; the same Intel ucode images)
iwlwifi-4965-2.ucode		iwn-4965
iwlwifi-5000-5.ucode		iwn-5000
iwlwifi-5150-2.ucode		iwn-5150
iwlwifi-1000-5.ucode		iwn-1000
iwlwifi-6000-4.ucode		iwn-6000
iwlwifi-6050-5.ucode		iwn-6050
iwlwifi-6000g2a-6.ucode		iwn-6005
iwlwifi-6000g2b-6.ucode		iwn-6030
iwlwifi-2030-6.ucode		iwn-2030
iwlwifi-2000-6.ucode		iwn-2000
iwlwifi-3160-17.ucode		iwm-3160-17
iwlwifi-3168-29.ucode		iwm-3168-29
iwlwifi-7260-17.ucode		iwm-7260-17
iwlwifi-7265-17.ucode		iwm-7265-17
iwlwifi-7265D-29.ucode		iwm-7265D-29
iwlwifi-8000C-36.ucode		iwm-8000C-36
iwlwifi-8265-34.ucode		iwm-8265-34
iwlwifi-9260-th-b0-jf-b0-34.ucode	iwm-9260-34
LICENCE.iwlwifi_firmware	LICENCE.iwlwifi_firmware

# Intel PRO/Wireless 3945, etherwpi (its header is the 3945 ucode's)
iwlwifi-3945-2.ucode		wpi-3945abg

# Ralink RT2860, etherrt2860 (the 8 KB microcode)
rt2860.bin			ral-rt2860
LICENCE.ralink-firmware.txt	LICENCE.ralink-firmware.txt

# Realtek WLAN, aux/rtw89
rtw89/*				rtw89/
LICENCE.rtlwifi_firmware.txt	LICENCE.rtlwifi_firmware.txt

# GPUs (the profiles' gpu group): aux/amdgpu, aux/i915
amdgpu/*			amdgpu/
LICENSE.amdgpu			LICENSE.amdgpu
i915/*				i915/
LICENSE.i915			LICENSE.i915
