// Посилання «Перейти до вмісту» переводить фокус у головний блок, не змінюючи адресу (без #main у рядку браузера).
document.querySelectorAll('a.skip').forEach((link) => {
  link.addEventListener('click', (event) => {
    const main = document.getElementById('main');
    if (!main) {
      return;
    }
    event.preventDefault();
    main.tabIndex = -1;
    main.focus({ preventScroll: true });
    main.scrollIntoView({ block: 'start' });
  });
});
