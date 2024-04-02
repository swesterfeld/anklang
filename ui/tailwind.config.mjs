// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

export default {
  content: [
    'ui/*.html', 'ui/*.*js', 'ui/*.*css',
    'ui/b/*.*js', 'ui/b/*.vue',
  ],
  theme: {
    borderColor: ({ theme }) => ({
      ...theme ('colors'),
      DEFAULT: 'var(--tw-border-default-color)', // theme ('colors.gray.200', 'currentColor'),
    }),
    extend: {
      colors: {
        dim: {
	  50:  '#f9f9ff',
	  100: '#f3f3fe',
	  200: '#e3e3ef',
	  300: '#c7c7d2',
	  400: '#a0a0aa',
	  500: '#76767f',
	  600: '#515159',
	  700: '#36363e',
	  800: '#26262e',
	  900: '#16161e',
	  950: '#07060f',
	},
      },
    },
  },
};
