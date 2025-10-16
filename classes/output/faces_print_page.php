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
 * Renderable for the printable Faces page grouped by course groups.
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
use templatable;
use user_picture;

/**
 * Renderable that shows printable faces grouped by course groups.
 */
class faces_print_page implements renderable, templatable {

    /**
     * Constructor.
     *
     * @param \stdClass $course
     * @param array $groupids
     * @param string $orderby
     */
    public function __construct(
        private \stdClass $course,
        private array $groupids,
        private string $orderby
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

        $selectedgroupids = array_values(array_unique(array_map('intval', $this->groupids)));
        $selectedgroups = [];
        foreach ($selectedgroupids as $groupid) {
            if ($groupid <= 0) {
                continue;
            }
            if (!$group = groups_get_group($groupid, '*')) {
                continue;
            }
            if (!groups_group_visible($group, $this->course, $context)) {
                continue;
            }
            $selectedgroups[$groupid] = $group;
        }

        $formattedselected = array_keys($selectedgroups);

        $groupings = [];
        $usedgroupids = [];
        $coursegroupings = groups_get_all_groupings($this->course->id);
        if (!empty($coursegroupings)) {
            foreach ($coursegroupings as $grouping) {
                $groupinggroups = groups_get_all_groups($this->course->id, 0, $grouping->id, 'g.*');
                $groupitems = [];
                foreach ($groupinggroups as $group) {
                    if (!groups_group_visible($group, $this->course, $context)) {
                        continue;
                    }
                    $usedgroupids[$group->id] = true;
                    $groupitems[] = [
                        'id' => (int)$group->id,
                        'name' => format_string($group->name, true, ['context' => $context]),
                        'checked' => in_array((int)$group->id, $formattedselected, true),
                    ];
                }
                if (!empty($groupitems)) {
                    $groupings[] = [
                        'id' => (int)$grouping->id,
                        'name' => format_string($grouping->name, true, ['context' => $context]),
                        'groups' => $groupitems,
                    ];
                }
            }
        }

        $allgroups = groups_get_all_groups($this->course->id, 0, 0, 'g.*');
        $ungroupedgroups = [];
        foreach ($allgroups as $group) {
            if (isset($usedgroupids[$group->id])) {
                continue;
            }
            if (!groups_group_visible($group, $this->course, $context)) {
                continue;
            }
            $ungroupedgroups[] = [
                'id' => (int)$group->id,
                'name' => format_string($group->name, true, ['context' => $context]),
                'checked' => in_array((int)$group->id, $formattedselected, true),
            ];
        }

        if (!empty($ungroupedgroups)) {
            $groupings[] = [
                'id' => 0,
                'name' => get_string('printgroupsungrouped', 'block_faces'),
                'groups' => $ungroupedgroups,
                'isungrouped' => true,
            ];
        }

        $fields = fields::for_name()->with_userpic();
        $requiredfields = array_diff($fields->get_required_fields(), ['id']);
        $fieldlist = 'u.id';
        if (!empty($requiredfields)) {
            $fieldlist .= ',' . implode(',', $requiredfields);
        }

        $sections = [];
        foreach ($selectedgroups as $group) {
            $users = get_enrolled_users($context, '', (int)$group->id, $fieldlist, '', 0, 0, true);
            $users = array_values($users);
            core_collator::asort_objects_by_property($users, $this->orderby, core_collator::SORT_NATURAL);

            $items = [];
            foreach ($users as $user) {
                $picture = new user_picture($user);
                $picture->size = 100;
                $items[] = [
                    'fullname' => fullname($user, true),
                    'picture' => $picture->get_url($PAGE)->out(false),
                    'profileurl' => (new moodle_url('/user/view.php', [
                        'id' => $user->id,
                        'course' => $this->course->id,
                    ]))->out(false),
                ];
            }

            $sections[] = [
                'groupid' => (int)$group->id,
                'groupname' => format_string($group->name, true, ['context' => $context]),
                'users' => $items,
                'hasusers' => !empty($items),
                'nousers' => get_string('nousers', 'block_faces'),
            ];
        }

        return [
            'courseid' => $this->course->id,
            'coursename' => format_string($this->course->fullname, true, ['context' => $context]),
            'currentdate' => userdate(time(), get_string('strftimedate', 'langconfig')),
            'actionurl' => (new moodle_url('/blocks/faces/print/page.php', [
                'cid' => $this->course->id,
                'orderby' => $this->orderby,
            ]))->out(false),
            'orderby' => $this->orderby,
            'groupings' => $groupings,
            'hasgroupings' => !empty($groupings),
            'sections' => $sections,
            'hassections' => !empty($sections),
            'nogroupsselected' => get_string('printnogroupsselected', 'block_faces'),
            'nogroupsavailable' => get_string('printnogroupsavailable', 'block_faces'),
            'showreset' => !empty($sections),
            'reseturl' => (new moodle_url('/blocks/faces/print/page.php', [
                'cid' => $this->course->id,
                'orderby' => $this->orderby,
            ]))->out(false),
            'submitlabel' => get_string('printapplyselection', 'block_faces'),
            'resetlabel' => get_string('printresetselection', 'block_faces'),
        ];
    }
}
