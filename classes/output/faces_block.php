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
 * Renderable for the Faces block content.
 *
 * @package   block_faces
 * @copyright 2025 Moodle
 * @license   http://www.gnu.org/copyleft/gpl.html GNU GPL v3 or later
 */

namespace block_faces\output;

use moodle_url;
use renderable;
use renderer_base;
use templatable;

class faces_block implements renderable, templatable {

    /**
     * Constructor.
     *
     * @param int $courseid
     */
    public function __construct(private int $courseid) {
    }

    /**
     * Export data for the block template.
     *
     * @param renderer_base $output
     * @return array
     */
    public function export_for_template(renderer_base $output): array {
        $url = new moodle_url('/blocks/faces/showfaces/show.php', ['cid' => $this->courseid]);

        return [
            'showfacesurl' => $url->out(false),
            'imageurl' => (new moodle_url('/blocks/faces/faces.png'))->out(false),
            'linktext' => get_string('showallfaces', 'block_faces'),
        ];
    }
}
