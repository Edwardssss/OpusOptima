// Minimal bilingual switch for the static page. Elements carry data-en/data-zh;
// dynamic code uses OpusI18n.lang or the tiny t() helper.
(() => {
  const saved = localStorage.getItem('opusoptima.lang');
  const lang = saved === 'zh' || saved === 'en' ? saved : (navigator.language || '').startsWith('zh') ? 'zh' : 'en';
  const api = {
    lang,
    t(en, zh) { return this.lang === 'zh' ? zh : en; },
    error(message) {
      const text = String(message ?? '');
      if (this.lang !== 'zh') return text;
      const known = [
        [/Standard solver found no candidate/i, '未找到可求解的候选'],
        [/Not an Opus Magnum puzzle file/i, '不是有效的 Opus Magnum puzzle 文件'],
        [/invalid puzzle input/i, 'puzzle 输入无效'],
        [/invalid trace input/i, '轨迹输入无效'],
        [/invalid describe input/i, '描述输入无效'],
        [/out of memory/i, '内存不足'],
        [/solution did not complete/i, '解未在限制内完成'],
        [/omsim rejected/i, 'omSim 拒绝了候选'],
        [/verification failed/i, '验证失败'],
        [/puzzle or solution parse failed/i, 'puzzle 或 solution 解析失败'],
        [/solution decode failed/i, 'solution 解码失败'],
        [/puzzle build failed/i, 'puzzle 构建失败'],
        [/editor build failed/i, '编辑器构建失败'],
        [/invalid .* coordinate/i, '坐标无效'],
        [/must be/i, '字段格式不正确'],
      ];
      for (const [pattern, chinese] of known) if (pattern.test(text)) return `错误：${chinese}`;
      return `错误：${text}`;
    },
    set(next) {
      this.lang = next;
      localStorage.setItem('opusoptima.lang', next);
      this.apply();
      window.dispatchEvent(new CustomEvent('opus-lang', { detail: next }));
    },
    toggle() { this.set(this.lang === 'zh' ? 'en' : 'zh'); },
    apply() {
      const zh = this.lang === 'zh';
      document.documentElement.lang = zh ? 'zh-CN' : 'en';
      for (const el of document.querySelectorAll('[data-en]')) {
        const value = zh ? el.dataset.zh : el.dataset.en;
        if (value !== undefined) el.innerHTML = value;
      }
      for (const el of document.querySelectorAll('[data-en-placeholder]')) {
        const value = zh ? el.dataset.zhPlaceholder : el.dataset.enPlaceholder;
        if (value !== undefined) el.setAttribute('placeholder', value);
      }
      for (const el of document.querySelectorAll('[data-en-title]')) {
        const value = zh ? el.dataset.zhTitle : el.dataset.enTitle;
        if (value !== undefined) el.setAttribute('title', value);
      }
      const toggle = document.getElementById('lang-toggle');
      if (toggle) toggle.textContent = zh ? 'EN' : '中文';
    },
  };
  window.OpusI18n = api;
  if (typeof document !== 'undefined') {
    document.addEventListener('DOMContentLoaded', () => api.apply());
    if (document.readyState !== 'loading') api.apply();
    document.addEventListener('click', (event) => {
      if (event.target?.id === 'lang-toggle') api.toggle();
    });
  }
})();
