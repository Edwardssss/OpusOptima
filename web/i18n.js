// Minimal bilingual switch for the static page. Elements carry data-en/data-zh;
// dynamic code uses OpusI18n.lang or the tiny t() helper.
(() => {
  const saved = localStorage.getItem('opusoptima.lang');
  const lang = saved === 'zh' || saved === 'en' ? saved : (navigator.language || '').startsWith('zh') ? 'zh' : 'en';
  const api = {
    lang,
    t(en, zh) { return this.lang === 'zh' ? zh : en; },
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
