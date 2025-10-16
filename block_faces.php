<?php
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
 * Faces block definition.
 *
 * @package   block_faces
 * @copyright 2025 Moodle
 * @license   http://www.gnu.org/copyleft/gpl.html GNU GPL v3 or later
 */

defined('MOODLE_INTERNAL') || die();

class block_faces extends block_base {

    /**
     * Initialise the block.
     */
    public function init() {
        $this->title = get_string('pluginname', 'block_faces');
    }

    /**
     * Returns the block contents.
     *
     * @return stdClass
     */
    public function get_content() {
        global $OUTPUT;

        if ($this->content !== null) {
            return $this->content;
        }

        $this->content = new stdClass();
        $this->content->text = '';
        $this->content->footer = '';

        $context = $this->page->context;

        if (!$context instanceof context_course) {
            return $this->content;
        }

        if (!has_capability('block/faces:view', $context)) {
            return $this->content;
        }

        $renderable = new \block_faces\output\faces_block($context->instanceid);
        $this->content->text = $OUTPUT->render($renderable);

        return $this->content;
    }

    /**
     * Limit the formats where the block can be added.
     *
     * @return array
     */
    public function applicable_formats() {
        return ['course-view' => true];
    }

    /**
     * Allow a single instance per course.
     *
     * @return bool
     */
    public function instance_allow_multiple() {
        return false;
    }
}
