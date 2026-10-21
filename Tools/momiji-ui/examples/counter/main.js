// MomijiUI の見本（ブラウザ）。説明書 Docs/Guides/Manuals/momiji-ui-manual.md §3 に載せたコードはこのファイルが正。
// 画面は counter.json（UE の見本 SMomijiUICounterExample と同じファイル）。host の仕事は 4 つだけ:
//   1. 画面 JSON を読む  2. 状態から値を作って描く（MomijiUICanvas.present）  3. 押された action で状態を変える  4. 描き直す
// 入力（ポインタ・キー・文字欄の <input>・窓に合わせる）は MomijiUICanvas.attach に任せる。
import { MomijiUICanvas } from '../momiji-ui.mjs';

const canvas = document.getElementById('screen');
const R = new MomijiUICanvas.Renderer(canvas);
R.setTheme('midnight');
R.screenId = 'counter';   // 画面の名前（動きの鍵の接頭辞・画面ごとの状態の鍵。UE の見本と同じ）
R.domTextInputs = true;   // 文字欄の文字は <input> が出す（attach が重ねる）

// ---- 状態（host が持つ）----
const state = { count: 0, names: [] };

// ---- 1. 画面 JSON を読む（診断して、誤りがあれば止まる）。書体を読み終えてから描く ----
const res = await fetch('../counter.json');
if (!res.ok) throw new Error('画面 JSON が読めない: ' + res.status + ' ' + res.url);
const { root: tree } = MomijiUICanvas.load(await res.json());
await MomijiUICanvas.loadFonts();

// ---- 2. 状態から値を作って描く ----
function values() {
  return {
    strings: { title: 'momiji-ui の見本' },
    vars: { count: state.count },   // 条件式（enabledIf / showIf）にも {count} にも使える
    lists: { names: state.names.length ? state.names.map((n) => ({ kind: 'name', text: n })) : [{ kind: 'empty' }] },
  };
}
function render() { MomijiUICanvas.present(R, tree, values(), '#0e1116'); }

// ---- 3. 押された action で状態を変える（action の意味は host が決める）----
const host = MomijiUICanvas.attach(R, canvas, {
  stage: document.getElementById('stage'),
  render,
  fit: true,   // 窓に合わせる（resize と高 DPI も attach が受ける）
  onEvent(ev) {
    if (ev.action === 'count:add') state.count++;
    else if (ev.action === 'count:reset') state.count = 0;
    else if (ev.action === 'name:add') {
      const name = ev.value.trim();   // kind: 'submit' の value は文字欄（bindVar "name"）の中身
      if (name) state.names.push(name);
      host.setInputValue('name', '');
    }
    render();   // ---- 4. 描き直す ----
  },
});
R.onImageLoad = render;   // 画像は後から届くので、届いたら描き直す

window.example = { state, renderer: R, host };   // 確かめる口（Tools/momiji-ui/examples/check.mjs が使う）
