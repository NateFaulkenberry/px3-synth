#include "WavetableLibrary.h"

namespace px3
{
namespace
{
constexpr const char* kMagic = "PX3WT";

// A name that came from a dropped file's name, made safe to be a file name
// again. Without this an import called "bass/lead.wav" writes to a directory
// that does not exist and reports a mysterious failure.
juce::String sanitise(const juce::String& name)
{
    auto cleaned = juce::File::createLegalFileName(name.trim());
    cleaned = cleaned.removeCharacters("/\\:");
    return cleaned.isEmpty() ? juce::String("Untitled") : cleaned.substring(0, 64);
}
} // namespace

juce::File WavetableLibrary::userDirectory()
{
    // Alongside the presets, under the same product directory, so uninstalling
    // finds it and a user looking for their own content finds it too.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("P(X3)")
        .getChildFile("Wavetables");
}

bool WavetableLibrary::save(const juce::String& name,
                            const std::vector<FrameSpectrum>& frames,
                            juce::String& error)
{
    if (frames.empty())
    {
        error = "There are no frames to save.";
        return false;
    }

    const auto directory = userDirectory();
    if (! directory.isDirectory() && ! directory.createDirectory())
    {
        error = "Could not create " + directory.getFullPathName();
        return false;
    }

    const auto file = directory.getChildFile(sanitise(name) + kFileExtension);
    juce::TemporaryFile temporary(file);

    {
        juce::FileOutputStream stream(temporary.getFile());
        if (! stream.openedOk())
        {
            error = "Could not write " + file.getFullPathName();
            return false;
        }

        // Every frame is padded to the widest one, so the reader does not need
        // a length per frame and a truncated file cannot be mistaken for a
        // valid shorter one.
        int harmonics = 0;
        for (const auto& frame : frames)
        {
            harmonics = juce::jmax(harmonics, frame.harmonicCount());
        }

        stream.write(kMagic, 5);
        stream.writeInt(kFormatVersion);
        stream.writeInt(static_cast<int>(frames.size()));
        stream.writeInt(harmonics);

        for (const auto& frame : frames)
        {
            for (int h = 1; h <= harmonics; ++h)
            {
                const auto index = static_cast<std::size_t>(h);
                stream.writeFloat(index < frame.amplitude.size() ? frame.amplitude[index] : 0.0f);
                stream.writeFloat(index < frame.phase.size() ? frame.phase[index] : 0.0f);
            }
        }

        if (stream.getStatus().failed())
        {
            error = stream.getStatus().getErrorMessage();
            return false;
        }
    }

    // Written to a temporary and moved into place, so an interrupted save
    // leaves the previous table rather than half of a new one.
    if (! temporary.overwriteTargetFileWithTemporary())
    {
        error = "Could not replace " + file.getFullPathName();
        return false;
    }

    error.clear();
    return true;
}

juce::StringArray WavetableLibrary::userTableNames()
{
    juce::StringArray names;
    const auto directory = userDirectory();
    if (! directory.isDirectory())
    {
        return names;
    }

    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false,
                                                     juce::String("*") + kFileExtension))
    {
        names.add(file.getFileNameWithoutExtension());
    }
    names.sort(true);
    return names;
}

std::shared_ptr<const Wavetable> WavetableLibrary::load(const juce::String& name)
{
    const auto file = userDirectory().getChildFile(sanitise(name) + kFileExtension);
    if (! file.existsAsFile())
    {
        return nullptr;
    }

    juce::FileInputStream stream(file);
    if (! stream.openedOk())
    {
        return nullptr;
    }

    char magic[6] = {};
    if (stream.read(magic, 5) != 5 || juce::String(magic) != kMagic)
    {
        return nullptr;
    }

    if (stream.readInt() != kFormatVersion)
    {
        return nullptr;
    }

    const auto frameCount = stream.readInt();
    const auto harmonics = stream.readInt();
    if (frameCount < 1 || frameCount > Wavetable::kMaxFrameCount
        || harmonics < 1 || harmonics > Wavetable::kFrameSize)
    {
        return nullptr;
    }

    std::vector<FrameSpectrum> frames;
    frames.reserve(static_cast<std::size_t>(frameCount));

    for (int f = 0; f < frameCount; ++f)
    {
        FrameSpectrum frame;
        frame.amplitude.assign(static_cast<std::size_t>(harmonics) + 1, 0.0f);
        frame.phase.assign(static_cast<std::size_t>(harmonics) + 1, 0.0f);

        for (int h = 1; h <= harmonics; ++h)
        {
            frame.amplitude[static_cast<std::size_t>(h)] = stream.readFloat();
            frame.phase[static_cast<std::size_t>(h)] = stream.readFloat();
        }

        if (stream.getStatus().failed())
        {
            return nullptr;   // truncated: no table rather than a partial one
        }

        frames.push_back(std::move(frame));
    }

    return Wavetable::build(name, "USER", frames);
}

bool WavetableLibrary::remove(const juce::String& name)
{
    const auto file = userDirectory().getChildFile(sanitise(name) + kFileExtension);
    return file.existsAsFile() && file.deleteFile();
}

} // namespace px3
