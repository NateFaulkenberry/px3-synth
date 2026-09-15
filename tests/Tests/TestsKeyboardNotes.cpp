#include "TestSupport.h"

// testKeyboardNotes - a key can never be left held. A note-off clears a key
// however many note-ons it had, the on-screen keyboard's note-offs always fit in
// the queue, and the keyboard releases a held key on every path that stops
// tracking it.

namespace px3tests
{
namespace
{

bool keyHeld(PX3SynthAudioProcessor& processor, int midiNote)
{
    return processor.copyActiveNoteStates()[static_cast<std::size_t>(midiNote - PianoKeyboard::firstMidiNote)];
}

void processOneBlock(PX3SynthAudioProcessor& processor, juce::MidiBuffer& midi)
{
    juce::AudioBuffer<float> buffer(2, kBlockSize);
    buffer.clear();
    processor.processBlock(buffer, midi);
}

// A mouse event the keyboard will accept. Built by hand because the test has no
// message loop to generate one.
juce::MouseEvent keyboardEvent(PianoKeyboard& keyboard, juce::Point<float> position)
{
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            position, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &keyboard, &keyboard, juce::Time::getCurrentTime(),
                            position, juce::Time::getCurrentTime(), 1, false);
}

} // namespace

void testKeyboardNotes()
{
    suite("KEYBOARD NOTES");

    // Two note-ons for one key - MIDI thru, an overlapping recorded note, a
    // hardware key and a mouse click - and one note-off. The synth releases
    // every voice on the note at that note-off, so the key has to go dark too.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        juce::MidiBuffer on;
        on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 10);
        processOneBlock(processor, on);
        const auto heldAfterTwoOns = keyHeld(processor, 60);

        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        processOneBlock(processor, off);
        const auto heldAfterOneOff = keyHeld(processor, 60);

        check("Keyboard_ANoteOffClearsAKeyThatHadTwoNoteOns", heldAfterTwoOns && ! heldAfterOneOff,
              juce::String("held after two note-ons: ") + (heldAfterTwoOns ? "yes" : "NO")
                  + ", after one note-off: " + (heldAfterOneOff ? "STILL HELD" : "released"));
    }

    // The on-screen keyboard's queue fills whenever the host is not running
    // audio and the keys are still being played. Note-ons may be dropped then;
    // the note-off that releases a key must not be.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        processor.queueVirtualKeyboardNoteOn(60, 0.8f);
        for (int i = 0; i < 400; ++i)
        {
            processor.queueVirtualKeyboardNoteOn(62 + (i % 20), 0.8f);
        }
        processor.queueVirtualKeyboardNoteOff(60);

        juce::MidiBuffer empty;
        processOneBlock(processor, empty);

        check("Keyboard_ANoteOffIsNeverDroppedWhenTheQueueIsFull", ! keyHeld(processor, 60),
              juce::String("after flooding the queue with note-ons, the released key is ")
                  + (keyHeld(processor, 60) ? "STILL HELD" : "released"));
    }

    const auto pressAKey = [](PianoKeyboard& keyboard, int& pressed, int& released, int& releases)
    {
        keyboard.setBounds(0, 0, 1040, 120);
        keyboard.onNoteOn = [&pressed](int note, float) { pressed = note; };
        keyboard.onNoteOff = [&released, &releases](int note) { released = note; ++releases; };
        keyboard.mouseDown(keyboardEvent(keyboard, { 520.0f, 110.0f }));
    };

    // Bypassing every oscillator silences the keyboard. It used to forget the
    // held key without sending its note-off, leaving the note on for good.
    {
        PianoKeyboard keyboard;
        auto pressed = -1, released = -1, releases = 0;
        pressAKey(keyboard, pressed, released, releases);
        keyboard.setSilenced(true);

        check("Keyboard_SilencingReleasesTheHeldKey",
              pressed >= PianoKeyboard::firstMidiNote && released == pressed && releases == 1,
              "pressed " + juce::String(pressed) + ", released " + juce::String(released)
                  + " (" + juce::String(releases) + " note-off)");
    }

    // A mouse-up the host never delivers. With no button actually down, the
    // next animation frame ends the press.
    {
        PianoKeyboard keyboard;
        auto pressed = -1, released = -1, releases = 0;
        pressAKey(keyboard, pressed, released, releases);
        const auto releasedBeforeFrame = released;
        keyboard.debugAdvanceAnimationFrame();

        check("Keyboard_AMouseUpThatNeverArrivesIsReleasedOnTheNextFrame",
              pressed >= PianoKeyboard::firstMidiNote && releasedBeforeFrame == -1 && released == pressed,
              "pressed " + juce::String(pressed) + ", released before the frame "
                  + juce::String(releasedBeforeFrame) + ", after it " + juce::String(released));
    }

    // Every path goes through one release, so doing it twice - a mouse-up and
    // then the editor closing - sends one note-off, not two.
    {
        PianoKeyboard keyboard;
        auto pressed = -1, released = -1, releases = 0;
        pressAKey(keyboard, pressed, released, releases);
        keyboard.mouseUp(keyboardEvent(keyboard, { 520.0f, 110.0f }));
        keyboard.releaseHeldNote();

        check("Keyboard_ReleasingTwiceSendsOneNoteOff", pressed >= PianoKeyboard::firstMidiNote && releases == 1,
              juce::String(releases) + " note-off(s) for one press");
    }
}

} // namespace px3tests
