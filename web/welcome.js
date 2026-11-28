// First visit: a welcome that says what Frost and OpenGP5 are, and a guided tour (tour.js) that walks
// through every page of the site, opening each one, down to how to install and update the firmware. The tour opens
// again from Tutorial in the menu. Nothing here touches the pedal.
import { Tour } from './tour.js?v=1.9.1';

const SEEN = 'frost.welcome';
const $ = s => document.querySelector(s);
const shown = s => { const e = $(s); return e && !e.hidden && e.getClientRects().length ? e : null; };

/** The steps of the site tour; go(view) opens a page as the menu does. */
export function siteSteps(go) {
  const page = (view, sel, title, text) => ({ enter: () => go(view), target: () => shown(sel), title, text });
  return [
    { target: () => null, title: 'A tour of Frost',
      text: 'In a few steps you see every page and what it is for. Use Next and Back (or the arrow keys) and close it at any time with ✕ or Esc. The tour never changes anything on your pedal.' },
    { target: () => $('.sidebar'), title: 'The pages',
      text: 'This menu has every page of the studio. Below it are the Firmware page, the credits and Tutorial, which opens this tour again.' },
    { target: () => $('.topright'), title: 'Connect the GP-5',
      text: 'Plug the GP-5 into the computer with a USB data cable and choose Connect GP-5 (Chrome, Edge or Opera on a computer). The light beside it says what is connected. With the original Valeton firmware, install Frost first: the Firmware page shows how.' },
    { target: () => $('.lang'), title: 'Language and look',
      text: 'Switch between English and Portuguese here. Next to it, the skin changes the colours of the studio.' },
    { enter: () => go('live'), target: () => shown('#live-panel') || shown('#live-empty'), title: 'Editor',
      text: 'The effects chain of the preset playing on the pedal, in six slots. Click a block to change its effect and knobs live: the pedal follows at once. When the sound is right, save it as a preset. Without a pedal, Try the demo shows how it works.' },
    page('models', '#view-models .heading', 'NAMs & IRs',
      'Amp models and cabinets stored on the pedal. Drop a NAM capture to turn it into an amp model, or a .wav impulse response to make a cabinet, then choose it in the Editor. Each one can be renamed, downloaded or shared with the community.'),
    page('presets', '#view-presets .heading', 'Presets',
      'The sounds saved on the pedal: load, reorder, rename, download or share them. Back up everything to one file here before big changes, such as a firmware update.'),
    page('setlists', '#view-setlists .heading', 'Songs and setlist',
      'Build songs from saved presets, arrange your set and play it with one footswitch. Drafts stay in this browser until you save them to the pedal.'),
    page('controller', '#view-controller .heading', 'Footswitch controller',
      'Set up an M-VAVE Chocolate Plus: three actions per footswitch (press, double press, hold for 2 s), B+C together to switch between Effects and Loop, and the colours of the GP-5 light. This page has its own step-by-step tutorial.'),
    page('looper', '#view-looper .heading', 'Looper',
      'Loop with the GP-5 as your audio interface: four tracks in sync, sections A, B and C, count-in, a drum machine with blues and shuffle swing, and WAV export. Choose Start audio with the pedal on USB; B+C on the controller enter Loop mode.'),
    page('community', '#view-community .heading', 'Community',
      "Loops, presets, amp models and cabinets shared by other players, with the name of who made them. Choose a nickname (no account), share yours, or record over someone's loop: your take becomes a reply. Every loop has a link that plays in any browser."),
    page('firmware', '#view-firmware .banner', 'Firmware: before installing or updating',
      "Back up your sounds first: on the Presets page with Frost already on the pedal, or in Valeton's own editor if it still has the original firmware. Close any other editor that uses the pedal."),
    page('firmware', '#view-firmware .grid2 > .panel', 'Install or update in one click',
      'When this copy of the site carries the Frost firmware, this button installs it or updates it to the newest version: confirm, wait about 90 seconds and the pedal restarts by itself. Keep it plugged in and powered the whole time.'),
    page('firmware', '#fw-drop', 'Or update from a file',
      'Got a newer .bin from a Frost release? Drop it here: the page checks what it is and tells you before anything is installed. Valeton\'s official firmware also goes here, to go back to stock.'),
    page('firmware', '#view-firmware .help', 'If an install stops halfway',
      'It can always be repeated: unplug the pedal, hold the footswitch and the PARA knob, plug it back in (update mode) and install again. The factory loader is never overwritten.'),
    { target: () => $('#site-tour'), title: 'That is it',
      text: 'The site updates itself: reloading the page brings the newest version, and new firmware arrives on the Firmware page. Credits and licences are on About & credits. To see this tour again, choose Tutorial here. Have fun playing!' },
  ];
}

let tour = null;
export function startSiteTour(go) {
  tour?.close();
  tour = new Tour(siteSteps(go), { onclose: () => { tour = null; } });
  tour.start();
}

function welcomeDialog(go) {
  const d = document.createElement('dialog');
  d.className = 'welcome'; d.setAttribute('aria-labelledby', 'welcome-title');
  d.innerHTML = `
    <div class="welcomehead"><img src="frost_logo.png" alt="" width="96" height="48"><div>
      <h2 id="welcome-title">Welcome to Frost</h2>
      <p>Free, open firmware for the Valeton GP-5 and a studio for it, right in the browser: edit sounds live, loop with a drum machine, play with a footswitch controller and share with other players.</p></div></div>
    <div class="welcomegrid">
      <article><h3>Frost</h3><p>The custom firmware for the GP-5 by aLeX (freelender): a six-slot effects chain with amp models made from NAM captures, cabinet IRs, presets, tuner and drop tune, and this website: the live editor, setlists, the footswitch controller, the looper and the community.</p></article>
      <article><h3>OpenGP5</h3><p>The open platform Frost runs on, with the GP-5's audio link and the web installer this site uses to put firmware on the pedal.</p></article>
    </div>
    <p class="welcomenote">Free and open source (MIT). Not made by Valeton. Nothing is installed on your pedal unless you confirm it on the Firmware page.</p>
    <div class="welcomeactions"><button class="btn ghost" type="button" data-act="skip">Skip</button><button class="btn primary" type="button" data-act="start">Let's start</button></div>`;
  const done = start => { try { localStorage.setItem(SEEN, '1'); } catch { /* private mode */ } d.close(); d.remove(); if (start) startSiteTour(go); };
  d.querySelector('[data-act="skip"]').onclick = () => done(false);
  d.querySelector('[data-act="start"]').onclick = () => done(true);
  d.addEventListener('cancel', () => { try { localStorage.setItem(SEEN, '1'); } catch { /* private mode */ } });
  return d;
}

/** The welcome on the first visit, and Tutorial in the menu. */
export function startWelcome(go) {
  const link = $('#site-tour');
  if (link) link.onclick = e => { e.preventDefault(); startSiteTour(go); };
  let seen = false; try { seen = !!localStorage.getItem(SEEN); } catch { seen = true; }
  if (seen) return;
  const d = welcomeDialog(go); document.body.append(d); d.showModal();
  d.querySelector('[data-act="start"]').focus();
}
