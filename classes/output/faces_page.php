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
 * Renderable for the Faces main page.
 *
 * @package   block_faces
 * @copyright 2025 Moodle
 * @license   http://www.gnu.org/copyleft/gpl.html GNU GPL v3 or later
 */

namespace block_faces\output;

use core_collator;
use core_user\fields;
use moodle_url;
use renderable;
use renderer_base;
use single_select;
use templatable;
use user_picture;

class faces_page implements renderable, templatable {

    /**
     * Constructor.
     *
     * @param \stdClass $course
     * @param int $groupid
     * @param string $orderby
     * @param bool $showfilters
     */
    public function __construct(
        private \stdClass $course,
        private int $groupid,
        private string $orderby,
        private bool $showfilters = true
    ) {
    }

    /**
     * Export the renderable data for the template.
     *
     * @param renderer_base $output
     * @return array
     */
    public function export_for_template(renderer_base $output): array {
        global $PAGE, $CFG;

        require_once($CFG->libdir . '/grouplib.php');

        $context = \context_course::instance($this->course->id);

        $validorders = ['firstname', 'lastname'];
        if (!in_array($this->orderby, $validorders, true)) {
            $this->orderby = 'firstname';
        }

        $orderurl = new moodle_url('/blocks/faces/showfaces/show.php', [
            'cid' => $this->course->id,
            'groupid' => $this->groupid,
        ]);
        $groupurl = new moodle_url('/blocks/faces/showfaces/show.php', [
            'cid' => $this->course->id,
            'orderby' => $this->orderby,
        ]);

        $orderoptions = [
            'firstname' => get_string('firstname', 'block_faces'),
            'lastname' => get_string('lastname', 'block_faces'),
        ];
        $groupoptions = [0 => get_string('showallfaces', 'block_faces')];
        $groups = groups_get_all_groups($this->course->id, 0, 0, 'g.id, g.name');
        foreach ($groups as $group) {
            $groupoptions[(int)$group->id] = format_string($group->name, true, ['context' => $context]);
        }

        $orderselect = new single_select($orderurl, 'orderby', $orderoptions, $this->orderby, null);
        $orderselect->set_label(get_string('orderby', 'block_faces'));
        $groupselect = new single_select($groupurl, 'groupid', $groupoptions, $this->groupid, null);
        $groupselect->set_label(get_string('filter', 'block_faces'));

        $fields = fields::for_name()->with_userpic();
        $requiredfields = array_diff($fields->get_required_fields(), ['id']);
        $fieldlist = 'u.id';
        if (!empty($requiredfields)) {
            $fieldlist .= ',' . implode(',', $requiredfields);
        }
        $users = get_enrolled_users($context, '', $this->groupid, $fieldlist, '', 0, 0, true);

        $users = array_values($users);
        core_collator::asort_objects_by_property($users, $this->orderby, core_collator::SORT_NATURAL);

        $items = [];
        foreach ($users as $user) {
            $picture = new user_picture($user);
            $picture->size = 100;
            $items[] = [
                'fullname' => fullname($user, true),
                'picture' => $picture->get_url($PAGE)->out(false),
                'profileurl' => (new moodle_url('/user/view.php', ['id' => $user->id, 'course' => $this->course->id]))->out(false),
            ];
        }

        $templatecontext = [
            'coursename' => format_string($this->course->fullname, true, ['context' => $context]),
            'currentdate' => userdate(time(), get_string('strftimedate', 'langconfig')),
            'users' => $items,
            'hasusers' => !empty($items),
            'nousers' => get_string('nousers', 'block_faces'),
            'showfilters' => $this->showfilters,
            'printurl' => (new moodle_url('/blocks/faces/print/page.php', [
                'cid' => $this->course->id,
                'groupid' => $this->groupid,
                'orderby' => $this->orderby,
            ]))->out(false),
            'printlabel' => get_string('print', 'block_faces'),
            'isprint' => !$this->showfilters,
        ];

        if ($this->showfilters) {
            $templatecontext['orderbyselect'] = $output->render($orderselect);
            $templatecontext['groupselect'] = $output->render($groupselect);
        }

        return $templatecontext;
    }
}
