// Frost web: the skin of the drawn gear and panels (rig.css, [data-skin] on <html>). Frost is the default;
// the choice is remembered in this browser.
const SKINS = ['frost', 'vintage', 'noir'];
const KEY = 'frost.skin';

export function startSkins() {
  let skin = 'frost';
  try { skin = localStorage.getItem(KEY) || 'frost'; } catch {}
  if (!SKINS.includes(skin)) skin = 'frost';
  document.documentElement.dataset.skin = skin;
  const pick = document.getElementById('skin');
  if (!pick) return;
  pick.value = skin;
  pick.addEventListener('change', () => {
    const next = SKINS.includes(pick.value) ? pick.value : 'frost';
    document.documentElement.dataset.skin = next;
    try { localStorage.setItem(KEY, next); } catch {}
  });
}
