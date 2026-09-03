-- PX3 Uninstaller.
--
-- Shipped as an application rather than a .pkg deliberately. The macOS
-- Installer always presents install-style UI - a Destination Select pane, an
-- Installation Type pane and an "Install" button - and none of that can be
-- relabelled from a Distribution file. An uninstaller delivered that way reads
-- as an installer no matter what the panes say. Owning the application means
-- owning every word the user sees.
--
-- It removes what the user selects, product by product, and it discovers what
-- is installed at run time rather than working from a list baked in when it
-- was built. That is what lets one uninstaller handle products released after
-- it, and installations left over from versions released before it.

on run
	set detectorPath to POSIX path of (path to resource "detect-au-hosts.sh")
	set removalPath to POSIX path of (path to resource "px3-uninstall.sh")
	set listPath to POSIX path of (path to resource "px3-list-products.sh")

	-- 1. Refuse while a known Audio Unit host is open. A host with a plug-in
	--    loaded holds the bundle open, and removing it underneath leaves that
	--    host running stale code.
	set runningHosts to do shell script "sh " & quoted form of detectorPath & " 2>/dev/null || true"
	if runningHosts is not "" then
		set hostList to my bulletList(runningHosts)
		display dialog "PX3 products cannot be removed while these applications are running:" & return & return & hostList & return & "Please save your work, close them, and run this uninstaller again." with title "PX3 Uninstaller" buttons {"OK"} default button 1 with icon caution
		return
	end if

	-- 2. What is actually on this machine.
	set installedRaw to do shell script "sh " & quoted form of listPath & " 2>/dev/null || true"
	if installedRaw is "" then
		display dialog "No PX3 products were found on this computer." & return & return & "There is nothing to remove." with title "PX3 Uninstaller" buttons {"OK"} default button 1
		return
	end if

	set productNames to my columnOne(installedRaw)
	if (count of productNames) is 0 then
		display dialog "No PX3 products were found on this computer." with title "PX3 Uninstaller" buttons {"OK"} default button 1
		return
	end if

	-- 3. Choose the products. Everything is selected to begin with, because
	--    "remove PX3" is the common case and unticking is less work than
	--    ticking eight things.
	set chosen to choose from list productNames with title "PX3 Uninstaller" with prompt "Select the PX3 products to remove from this computer:" default items productNames with multiple selections allowed
	if chosen is false then return
	if (count of chosen) is 0 then return

	-- 4. What happens to the presets. AppleScript dialogs have no checkbox, so
	--    this is two buttons - but the DEFAULT is to keep, and the keep button
	--    is the one the return key presses.
	set presetPrompt to "Keep your presets and imported wavetables?" & return & return & ¬
		"KEEP: your own saved presets and any wavetables you imported stay on this computer, ready for a future reinstall. Factory presets and settings are removed." & return & return & ¬
		"REMOVE EVERYTHING: all presets - your own AND factory - plus imported wavetables and settings are deleted. This cannot be undone."

	display dialog presetPrompt with title "PX3 Uninstaller" buttons {"Cancel", "Remove Everything", "Keep My Presets"} default button "Keep My Presets" cancel button "Cancel" with icon caution
	set presetChoice to button returned of result

	set keepPresets to "1"
	if presetChoice is "Remove Everything" then set keepPresets to "0"

	-- 5. Confirm, spelling out exactly what goes and what stays. The list is
	--    the products the user actually picked, not a generic sentence.
	set productList to my bulletList(my joinWithReturns(chosen))
	if keepPresets is "1" then
		set presetLine to "Your own presets and imported wavetables will be KEPT." & return & "Factory presets and settings will be removed."
	else
		set presetLine to "ALL PRESETS will be deleted - your own saved presets as well as the factory ones - along with imported wavetables and settings." & return & "Your saved presets cannot be recovered afterwards. If you want to keep any, quit now and export them first."
	end if

	set confirmText to "This will remove the following from this computer, for every user account:" & return & return & ¬
		productList & return & presetLine

	display dialog confirmText with title "PX3 Uninstaller" buttons {"Cancel", "Uninstall"} default button "Cancel" cancel button "Cancel" with icon caution

	-- 6. Remove, with the system's own authorisation prompt. The selection is
	--    passed in the environment rather than as arguments so a product name
	--    with a space in it - which every one of them has - cannot be split.
	set selectionArg to my joinWithBars(chosen)
	set removalCommand to "PX3_PRODUCTS=" & quoted form of selectionArg & ¬
		" PX3_KEEP_PRESETS=" & keepPresets & ¬
		" sh " & quoted form of removalPath

	try
		do shell script removalCommand with administrator privileges
	on error errorMessage number errorNumber
		if errorNumber is -128 then
			return -- the user cancelled the authorisation prompt
		end if
		display dialog "PX3 could not be fully removed." & return & return & errorMessage with title "PX3 Uninstaller" buttons {"OK"} default button 1 with icon stop
		return
	end try

	-- 7. Re-check: anything still present means something held it open. Asking
	--    the scanner again is the honest check - it looks in the same places
	--    the removal did, so it cannot report success against a shorter list.
	set stillThere to do shell script "sh " & quoted form of listPath & " 2>/dev/null || true"
	set leftovers to {}
	repeat with removedName in chosen
		if stillThere contains (contents of removedName) then set end of leftovers to contents of removedName
	end repeat

	if (count of leftovers) > 0 then
		display dialog "Some products could not be fully removed:" & return & return & ¬
			my bulletList(my joinWithReturns(leftovers)) & return & ¬
			"A log is at /tmp/px3-uninstall.log" with title "PX3 Uninstaller" buttons {"OK"} default button 1 with icon caution
		return
	end if

	-- No mention of the log file. It exists, and the failure dialog above points
	-- at it because there it is actionable, but on a clean removal the path to a
	-- temporary file is noise for the person reading it.
	set doneText to "PX3 has been removed." & return & return & "Quit and reopen any DAW to refresh its plug-in list."
	if keepPresets is "1" then
		set doneText to "PX3 has been removed." & return & return & ¬
			"Your presets and imported wavetables were kept, and will be found again if you reinstall." & return & return & ¬
			"Quit and reopen any DAW to refresh its plug-in list."
	end if
	display dialog doneText with title "PX3 Uninstaller" buttons {"OK"} default button 1
end run

-- First tab-separated column of each line: the product names the scanner found.
on columnOne(rawText)
	set output to {}
	repeat with aLine in paragraphs of rawText
		set theLine to contents of aLine
		if theLine is not "" then
			set AppleScript's text item delimiters to tab
			set parts to text items of theLine
			set AppleScript's text item delimiters to ""
			if (count of parts) > 0 then
				set theName to item 1 of parts
				if theName is not "" then set end of output to theName
			end if
		end if
	end repeat
	return output
end columnOne

on joinWithBars(theList)
	set AppleScript's text item delimiters to "|"
	set output to theList as text
	set AppleScript's text item delimiters to ""
	return output
end joinWithBars

on joinWithReturns(theList)
	set AppleScript's text item delimiters to return
	set output to theList as text
	set AppleScript's text item delimiters to ""
	return output
end joinWithReturns

on bulletList(rawText)
	set output to ""
	repeat with aLine in paragraphs of rawText
		if (contents of aLine) is not "" then
			set output to output & "	• " & (contents of aLine) & return
		end if
	end repeat
	return output
end bulletList
