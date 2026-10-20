#include "Brand.h"
#include "../Version.h"
#include "LicensePanel.h"

using namespace Theme;

LicensePanel::LicensePanel (LicenseManager& l) : lic (l)
{
    addAndMakeVisible (keyField);
    keyField.setFont (mono (15.0f, 0.04f));
    keyField.setJustification (juce::Justification::centred);
    keyField.setTextToShowWhenEmpty ("XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX", faint);
    keyField.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    keyField.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    keyField.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    keyField.setColour (juce::TextEditor::textColourId, text);
    keyField.onReturnKey = [this] { startActivation(); };
    keyField.onTextChange = [this] { repaint(); };
    refresh();
}

juce::Rectangle<float> LicensePanel::card() const
{
    return getLocalBounds().toFloat().withSizeKeepingCentre (560.0f, 330.0f);
}

juce::Rectangle<int> LicensePanel::activateBounds() const
{
    const auto c = card().toNearestInt();
    return { c.getCentreX() - 110, c.getY() + 214, 220, 42 };
}

juce::Rectangle<int> LicensePanel::buyBounds() const
{
    const auto c = card().toNearestInt();
    return { c.getCentreX() - 160, c.getY() + 268, 320, 24 };
}

juce::Rectangle<int> LicensePanel::closeBounds() const
{
    const auto c = card().toNearestInt();
    return { c.getRight() - 44, c.getY() + 10, 34, 34 };
}

void LicensePanel::resized()
{
    const auto c = card().toNearestInt();
    keyField.setBounds (c.getX() + 40, c.getY() + 150, c.getWidth() - 80, 40);
}

void LicensePanel::refresh()
{
    const auto st = lic.getStatus();
    keyField.setVisible (st != LicenseManager::Status::activated);
    if (! working && message.isEmpty() && st == LicenseManager::Status::expired)
    {
        message = fr ("The 7-day trial is over: WHALE mutes the sound for 2 s every 40 s until it is activated.");
        messageColour = orange;
    }
    repaint();
}

void LicensePanel::startActivation()
{
    if (working) return;
    if (keyField.getText().trim().isEmpty())
    {
        message = fr ("Paste your license key first (sent by e-mail after your purchase).");
        messageColour = orange;
        repaint();
        return;
    }
    working = true;
    message = fr ("Checking the key with Gumroad…");
    messageColour = cyan;
    repaint();
    juce::Component::SafePointer<LicensePanel> safe (this);
    lic.activateAsync (keyField.getText(), [safe] (LicenseManager::Result r)
    {
        if (safe == nullptr) return;
        safe->working = false;
        safe->message = r.message;
        safe->messageColour = r.ok ? green : red;
        safe->refresh();
    });
}

void LicensePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xb3040209));
    const auto c = card();
    g.setColour (juce::Colour (0xf70c0820));
    g.fillRoundedRectangle (c, 16.0f);
    g.setColour (juce::Colour (0xff6e50be));
    g.drawRoundedRectangle (c, 16.0f, 1.4f);

    // close cross
    {
        const auto cb = closeBounds().toFloat().reduced (11.0f);
        g.setColour (hover == 3 ? text : dim);
        g.drawLine (cb.getX(), cb.getY(), cb.getRight(), cb.getBottom(), 1.8f);
        g.drawLine (cb.getRight(), cb.getY(), cb.getX(), cb.getBottom(), 1.8f);
    }

    const int x = (int) c.getX() + 40, w = (int) c.getWidth() - 80, y = (int) c.getY();
    g.setColour (juce::Colour (0xffece4ff));
    g.fillPath (flukeGlyph (c.getCentreX(), (float) y + 38, 14));
    g.setColour (juce::Colours::white);
    g.setFont (display (20.0f, 0.04f));
    g.drawText ("WHALE MASTERING", x, y + 54, w, 28, juce::Justification::centred);
    g.setColour (faint);
    g.setFont (mono (10.5f, 0.12f));
    {
        const float lh = 16.0f, lw = Brand::lockupWidth (lh), lx = c.getCentreX() - lw * 0.5f;
        Brand::drawLockup (g, lx, c.getBottom() - 34.0f, lh, 0.9f);
        g.drawText (juce::String ("v") + WHALE_VERSION_STRING, (int) c.getRight() - 120, (int) c.getBottom() - 26, 100, 14, juce::Justification::centredRight);
    }

    const auto st = lic.getStatus();
    g.setFont (bold (14.0f));
    juce::String line;
    juce::Colour lineCol = dim;
    if (st == LicenseManager::Status::activated)
    {
        line = fr ("License activated") + (lic.getEmail().isNotEmpty() ? fr (" — ") + lic.getEmail() : juce::String());
        lineCol = green;
    }
    else if (st == LicenseManager::Status::trial)
    {
        const int d = lic.trialDaysLeft();
        line = fr ("Full trial version: ") + juce::String (d) + (d > 1 ? fr (" days left") : fr (" day left"));
        lineCol = cyan;
    }
    else
    {
        line = fr ("Trial over");
        lineCol = orange;
    }
    g.setColour (lineCol);
    g.drawText (line, x, y + 88, w, 22, juce::Justification::centred);

    if (st != LicenseManager::Status::activated)
    {
        g.setColour (dim);
        g.setFont (body (13.0f));
        g.drawText (fr ("License key (in the Gumroad e-mail you received after purchase):"), x, y + 122, w, 20, juce::Justification::centred);
        const auto kb = keyField.getBounds().toFloat();
        g.setColour (field);
        g.fillRoundedRectangle (kb, 8.0f);
        g.setColour (keyField.hasKeyboardFocus (true) ? magenta : lineHi);
        g.drawRoundedRectangle (kb, 8.0f, 1.2f);

        // ACTIVER
        const auto ab = activateBounds().toFloat();
        g.setColour (working ? lineHi : (hover == 1 ? magenta.brighter (0.2f) : magenta));
        g.fillRoundedRectangle (ab, ab.getHeight() * 0.5f);
        g.setColour (juce::Colours::white);
        g.setFont (bold (15.0f, 0.08f));
        g.drawText (working ? fr ("CHECKING…") : fr ("ACTIVATE"), ab.toNearestInt(), juce::Justification::centred);

        if (juce::String (LicenseManager::buyUrl).isNotEmpty())
        {
            g.setColour (hover == 2 ? text : cyan);
            g.setFont (bold (13.5f));
            g.drawText (fr ("No key yet? Buy WHALE Mastering (€49)"), buyBounds(), juce::Justification::centred);
        }
    }
    else
    {
        g.setColour (dim);
        g.setFont (body (13.0f));
        g.drawText (fr ("Thanks for your purchase! WHALE now works offline."), x, y + 150, w, 20, juce::Justification::centred);
    }

    if (message.isNotEmpty())
    {
        g.setColour (messageColour);
        g.setFont (body (12.5f));
        const int my = st == LicenseManager::Status::activated ? y + 186 : y + 292;
        g.drawFittedText (message, x - 10, my, w + 20, 34, juce::Justification::centred, 2);
    }
}

void LicensePanel::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int h = activateBounds().contains (p) && keyField.isVisible() ? 1
                : buyBounds().contains (p) && keyField.isVisible() && juce::String (LicenseManager::buyUrl).isNotEmpty() ? 2
                : closeBounds().contains (p) ? 3 : 0;
    if (h != hover) { hover = h; repaint(); }
}

void LicensePanel::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (closeBounds().contains (p) || ! card().contains (p.toFloat())) { if (onClose) onClose(); return; }
    if (! keyField.isVisible()) return;
    if (activateBounds().contains (p)) startActivation();
    else if (buyBounds().contains (p) && juce::String (LicenseManager::buyUrl).isNotEmpty())
        juce::URL (LicenseManager::buyUrl).launchInDefaultBrowser();
}
