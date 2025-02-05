<!-- This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0 -->

<docs>
  # B-DIALOG
  A dialog that captures focus and provides a modal shield that dims everything else.
  A *close* event is emitted on clicks outside of the dialog area,
  if *Escape* is pressed or the default *Close* button is actiavted.
  ## Props:
  *shown*
  : A boolean to control the visibility of the dialog, suitable for `v-model:shown` bindings.
  ## Events:
  *update:shown*
  : An *update:shown* event with value `false` is emitted when the "Close" button activated.
  ## Slots:
  *header*
  : The *header* slot can be used to hold the modal dialog title.
  *default*
  : The *default* slot holds the main content.
  *footer*
  : By default, the *footer* slot holds a *Close* button which emits the *close* event.
  ## CSS Classes:
  *b-dialog*
  : The *b-dialog* CSS class contains styling for the actual dialog contents.
  *b-dialog-modalshield*
  : This CSS class contains styling for masking content under the dialog.
</docs>

<style lang="scss">
.b-dialog-modalshield {
  position: fixed; inset: 0;
  width: 100%; height: 100%;
  display: flex;
  background: $b-style-modal-overlay;
  transition: opacity 0.3s ease;
  pointer-events: all;
}
.b-dialog {
  display: flex;
  margin: auto;
  max-height: 100%; max-width: 100%;
  min-width: 16em; min-width: min(100%,20em);
  border: 2px solid $b-modal-bordercol; border-radius: 5px;
  color: $b-modal-foreground; background: $b-modal-background;
  padding: 1em;
  box-shadow: 0 2px 8px $b-boxshadow-color;
  /* fix vscrolling: https://stackoverflow.com/a/33455342 */
  justify-content: space-between;
  overflow: auto;

  .-footer {
    button, push-button {
      &:first-child { margin-left: 1px; }
      &:last-child  { margin-right: 1px; }
    }
  }
  .-header {
    font-size: 1.5em;
    @include b-font-weight-bold();
    justify-content: space-evenly;
    padding-bottom: 0.5em;
    border-bottom: 1px solid $b-modal-bordercol;
  }
  .-header, .-footer {
    align-self: stretch;
    align-items: center; text-align: center;
  }
  .-footer {
    justify-content: space-evenly;
    padding-top: 1em;
    border-top: 1px solid $b-modal-bordercol;
    &.-empty { display: none; }
  }
  .-body {
    padding: 1em 1em;
    align-self: stretch;
  }
}

/* fading frames: [0] "v-{enter|leave}-from v-{enter|leave}-active" [1] "v-{enter|leave}-to v-{enter|leave}-active" [N] ""
 * https://v3.vuejs.org/guide/transitions-enterleave.html#transition-classes
 */
$duration: 0.3s;
.b-dialog-modalshield {
  &.-fade-enter-active, &.-fade-leave-active {
    &, .b-dialog {
      transition: all $duration ease, opacity $duration linear;
    }
  }
  &.-fade-enter-from, &.-fade-leave-to {
    opacity: 0;
    .b-dialog { transform: scale(0.1); }
  }
}

</style>

<template>
  <transition name="-fade">
    <div class="b-dialog-modalshield" v-if='shown' v-show='done_resizing()' @keydown="keydown" >
      <v-flex class="b-dialog floating-dialog" @click.stop ref='dialog' >

	<h-flex class="-header">
	  <slot name="header">
	    Modal Dialog
	  </slot>
	</h-flex>

	<v-flex class="-body" ref="body">
	  <slot name="default"></slot>
	</v-flex>

	<h-flex class="-footer" :class="footerclass" ref="footer">
	  <slot name="footer"/>
	</h-flex>

      </v-flex>
    </div>
  </transition>
</template>

<script>
import * as Util from "../util.js";

export default {
  sfc_template,
  props:     { shown: { type: Boolean },
	       exclusive: { type: Boolean },
	       bwidth: { default: '9em' }, },
  emits: { 'update:shown': null, 'close': null },
  data() { return {
    footerclass: '',
    childrenmounted_: false,
    done_resizing_: false,
    handled_autofocus_: false,
    b_dialog_resizers: [],
  }; },
  provide() { return { b_dialog_resizers: this.b_dialog_resizers }; },
  computed: {
  },
  mounted () {
    this.$forceUpdate(); // force updated() after mounted()
  },
  updated() {
    this.childrenmounted_ = !!this.$refs.body;
    let must_focus = false;
    // newly shown
    if (this.$refs.dialog && !this.undo_shield)
      {
	must_focus = true;
	this.undo_shield = Util.setup_shield_element (this.$el, this.$refs.dialog, this.close.bind (this), false);
      }
    // newly hidden
    if (!this.$refs.dialog && this.undo_shield)
      {
	this.undo_shield();
	this.undo_shield = null;
      }
    // adjust CSS
    if (this.$refs.dialog)
      {
	this.footerclass = this.$refs.footer && this.$refs.footer.innerHTML ? '' : '-empty';
	const sel = Util.vm_scope_selector (this);
	const css = [];
	if (this.bwidth) {
	  css.push (`${sel}.b-dialog .-footer button      { min-width: ${this.bwidth}; }`);
	  css.push (`${sel}.b-dialog .-footer push-button { min-width: ${this.bwidth}; }`);
	}
	Util.vm_scope_style (this, css.join ('\n'));
      }
    // autofocus
    if (this.$refs.dialog && this.undo_shield && !this.handled_autofocus_ && this.done_resizing_) {
      this.handled_autofocus_ = true;
      const seen_autofocus = this.$refs.dialog.querySelector ('[autofocus]:not([disabled]):not([display="none"])');
      if (seen_autofocus) {
	const next_tick_autofocus = async () => {
	  // wait for v-show=done_resizing to take effect
	  await Vue.nextTick();
	  // adjust to possible DOM changes
	  const autofocus_element = this.$refs.dialog.querySelector ('[autofocus]:not([disabled]):not([display="none"])');
	  autofocus_element?.focus();
	};
	next_tick_autofocus();
	must_focus = false;
      }
    }
    // force focus, so Escape can have an effect on closing the dialog
    if (must_focus)
      {
	must_focus = false;
	Util.move_focus ('HOME');
      }
  },
  unmount() {
    this.undo_shield?.();
    this.childrenmounted_ = false;
  },
  methods: {
    done_resizing() {
      if (this.done_resizing_)
	return this.done_resizing_;
      // children may introduce new b_dialog_resizers entries, so
      // done_resizing is `false` until children are present.
      if (!this.childrenmounted_)
	return false;
      for (const r of this.b_dialog_resizers)
	if (r.call (null))
	  return false;
      // resizing is done, but changing done_resizing_ may be ignored during render()
      this.done_resizing_ = true;
      // so force an update, this can only be triggered once anyway
      this.$forceUpdate();
      return true;
    },
    close (event) {
      this.$emit ('update:shown', false); // shown = false
      if (this.shown)
	this.$emit ('close');
    },
    keydown (event)
    {
      if (this.shown && event.keyCode === Util.KeyCode.ESCAPE)
	{
	  event.stopPropagation();
	  this.close (event);
	}
    },
  },
};
</script>
