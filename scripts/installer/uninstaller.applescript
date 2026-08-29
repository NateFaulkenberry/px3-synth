-- PX3 Uninstaller.
--
-- Shipped as an application rather than a .pkg deliberately. The macOS
-- Installer always presents install-style UI - a Destination Select pane, an
-- Installation Type pane and an "Install" button - and none of that can be
-- relabelled from a Distribution file. An uninstaller delivered that way reads
-- as an installer no matter what the panes say. Owning the application means
-- owning every word the user sees.

on run
	set detectorPath to POSIX path of (path to resource "detect-au-hosts.sh")
	set removalPath to POSIX path of (path to resource "px3-uninstall.sh")

	-- 1. Refuse while a known Audio Unit host is open. A host with the plug-in
	--    loaded holds the bundle open, and removing it underneath leaves that
	--    host running stale code.
	set runningHosts to do shell script "sh " & quoted form of detectorPath & " 2>/dev/null || true"
	if runningHosts is not "" then
		set hostList to my bulletList(runningHosts)
		display dialog "PX3 Synth cannot be removed while these applications are running:" & return & return & hostList & return & "Please save your work, close them, and run this uninstaller again." with title "PX3 Uninstaller" buttons {"OK"} default button 1 with icon caution
		return
	end if

	-- 2. Confirm, spelling out what goes. Presets are the part people do not
	--    expect to lose, so they are named first and named plainly.
	set confirmText to "This will completely remove PX3 Synth from this computer, for every user account:" & return & return & ¬
		"• The Audio Unit and VST3 plug-ins" & return & ¬
		"• The PX3 Synth application" & return & ¬
		"• ALL PRESETS — factory and your own saved presets" & return & ¬
		"• Preferences, caches and logs" & return & return & ¬
		"Your saved presets cannot be recovered afterwards. If you want to keep any, quit now and export them first."

	display dialog confirmText with title "PX3 Uninstaller" buttons {"Cancel", "Uninstall"} default button "Cancel" cancel button "Cancel" with icon caution

	-- 3. Remove, with the system's own authorisation prompt.
	try
		do shell script "sh " & quoted form of removalPath with administrator privileges
	on error errorMessage number errorNumber
		if errorNumber is -128 then
			return -- the user cancelled the authorisation prompt
		end if
		display dialog "PX3 Synth could not be fully removed." & return & return & errorMessage with title "PX3 Uninstaller" buttons {"OK"} default button 1 with icon stop
		return
	end try

	-- 4. Re-check: anything still present means something held it open.
	set leftovers to do shell script "ls -d '/Library/Audio/Plug-Ins/Components/PX3 Synth.component' '/Library/Audio/Plug-Ins/VST3/PX3 Synth.vst3' 2>/dev/null || true"
	if leftovers is not "" then
		display dialog "PX3 Synth was removed, but some files could not be deleted:" & return & return & leftovers & return & return & "A log is at /tmp/px3-uninstall.log" with title "PX3 Uninstaller" buttons {"OK"} default button 1 with icon caution
		return
	end if

	-- No mention of the log file. It exists, and the failure dialog above points
	-- at it because there it is actionable, but on a clean removal the path to a
	-- temporary file is noise for the person reading it.
	display dialog "PX3 Synth has been removed." & return & return & "Quit and reopen any DAW to refresh its plug-in list." with title "PX3 Uninstaller" buttons {"OK"} default button 1
end run

on bulletList(rawText)
	set output to ""
	repeat with aLine in paragraphs of rawText
		if (contents of aLine) is not "" then
			set output to output & "	• " & (contents of aLine) & return
		end if
	end repeat
	return output
end bulletList
