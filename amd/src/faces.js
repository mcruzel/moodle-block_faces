// This file is part of Moodle - http://moodle.org/
//
// Moodle is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Moodle is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Moodle.  If not, see <http://www.gnu.org/licenses/>.

/**
 * Faces block JavaScript helpers.
 *
 * @module     block_faces/faces
 * @copyright  2025 Moodle
 * @license    http://www.gnu.org/copyleft/gpl.html GNU GPL v3 or later
 */

const SELECTOR_SECTIONS_TOGGLE = '[data-action="toggle-sections"]';
const SELECTOR_GROUPING_TOGGLE = '[data-action="toggle-grouping"]';

const initSectionsToggle = () => {
    document.querySelectorAll(SELECTOR_SECTIONS_TOGGLE).forEach((button) => {
        const targetId = button.getAttribute('data-target');
        if (!targetId) {
            return;
        }

        const content = document.getElementById(targetId);
        if (!content) {
            return;
        }

        const setExpanded = (expanded) => {
            button.setAttribute('aria-expanded', expanded ? 'true' : 'false');
            content.hidden = !expanded;
        };

        setExpanded(false);

        button.addEventListener('click', (event) => {
            event.preventDefault();
            const expanded = button.getAttribute('aria-expanded') === 'true';
            setExpanded(!expanded);
        });
    });
};

const initGroupingToggles = () => {
    document.querySelectorAll(SELECTOR_GROUPING_TOGGLE).forEach((button) => {
        const grouping = button.closest('fieldset');
        if (!grouping) {
            return;
        }

        const checkboxes = Array.from(grouping.querySelectorAll('input[type="checkbox"]'));
        if (!checkboxes.length) {
            button.disabled = true;
            return;
        }

        const selectLabel = button.getAttribute('data-select-label') || '';
        const deselectLabel = button.getAttribute('data-deselect-label') || '';
        const text = button.querySelector('.faces-printgroups__grouping-toggle-text');

        const updateButtonState = () => {
            const checkedCount = checkboxes.filter((checkbox) => checkbox.checked).length;
            const shouldDeselect = checkedCount === checkboxes.length;
            button.setAttribute('aria-pressed', shouldDeselect ? 'true' : 'false');
            if (text) {
                text.textContent = shouldDeselect ? deselectLabel : selectLabel;
            }
        };

        button.addEventListener('click', (event) => {
            event.preventDefault();
            const checkedCount = checkboxes.filter((checkbox) => checkbox.checked).length;
            const shouldSelect = checkedCount !== checkboxes.length;
            checkboxes.forEach((checkbox) => {
                if (checkbox.checked !== shouldSelect) {
                    checkbox.checked = shouldSelect;
                    checkbox.dispatchEvent(new Event('change', {bubbles: true}));
                }
            });
            updateButtonState();
        });

        checkboxes.forEach((checkbox) => {
            checkbox.addEventListener('change', updateButtonState);
        });

        updateButtonState();
    });
};

export const init = () => {
    initSectionsToggle();
    initGroupingToggles();
};

export const initPrint = () => {
    window.print();
};
