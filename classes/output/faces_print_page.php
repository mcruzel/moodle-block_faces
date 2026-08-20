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

use block_faces\local\groups_helper;
use core_collator;
use core_user\fields;
use moodle_url;
use renderable;
use renderer_base;
use templatable;
use user_picture;

/**
 * Renderable that shows printable faces grouped by course groups.
 *
 * @package   block_faces
 * @copyright 2025 Moodle
 * @license   http://www.gnu.org/copyleft/gpl.html GNU GPL v3 or later
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
        global $USER;

        $context = \context_course::instance($this->course->id);
        $canseeall = groups_helper::can_see_all_participants($this->course, $context);
        $viewfullnames = has_capability('moodle/site:viewfullnames', $context);

        $validorders = ['firstname', 'lastname'];
        if (!in_array($this->orderby, $validorders, true)) {
            $this->orderby = 'firstname';
        }

        $groupdata = groups_helper::prepare_group_selection($this->course, $context, $this->groupids);
        $selectedgroups = $groupdata['selectedgroups'];

        $fields = fields::for_name()->with_userpic();
        $requiredfields = array_diff($fields->get_required_fields(), ['id']);
        $fieldlist = 'u.id';
        if (!empty($requiredfields)) {
            $fieldlist .= ',' . implode(',', $requiredfields);
        }

        // In separate groups mode without the accessallgroups capability the user must
        // never see the full participant list: when no group is selected, fall back to
        // the groups the current user belongs to.
        $sectiongroups = $selectedgroups;
        if (empty($sectiongroups) && !$canseeall) {
            $sectiongroups = groups_get_all_groups($this->course->id, $USER->id);
        }

        $sections = [];
        if (!empty($sectiongroups)) {
            foreach ($sectiongroups as $group) {
                $sections[] = $this->build_group_section($group, $context, $fieldlist, $viewfullnames);
            }
        } else {
            // Either the user may see everyone (default full list), or they belong to
            // no visible group: an empty list renders the 'nousers' notice.
            $items = $canseeall ? $this->build_user_items($context, 0, $fieldlist, $viewfullnames) : [];
            $sections[] = [
                'groupid' => 0,
                'groupname' => get_string('showallfaces', 'block_faces'),
                'users' => $items,
                'hasusers' => !empty($items),
                'nousers' => get_string('nousers', 'block_faces'),
                'isall' => true,
            ];
        }

        return [
            'coursename' => format_string($this->course->fullname, true, ['context' => $context]),
            'currentdate' => userdate(time(), get_string('strftimedate', 'langconfig')),
            'sections' => $sections,
            'hassections' => !empty($sections),
            'nogroupsselected' => get_string('printnogroupsselected', 'block_faces'),
        ];
    }

    /**
     * Build the template data for one group section.
     *
     * @param \stdClass $group The group record (id and name are required).
     * @param \context_course $context The course context.
     * @param string $fieldlist User fields to fetch.
     * @param bool $viewfullnames Whether the current user may see full names.
     * @return array
     */
    private function build_group_section(\stdClass $group, \context_course $context, string $fieldlist,
            bool $viewfullnames): array {
        $items = $this->build_user_items($context, (int)$group->id, $fieldlist, $viewfullnames);

        return [
            'groupid' => (int)$group->id,
            'groupname' => format_string($group->name, true, ['context' => $context]),
            'users' => $items,
            'hasusers' => !empty($items),
            'nousers' => get_string('nousers', 'block_faces'),
        ];
    }

    /**
     * Fetch, sort and export the enrolled users of one group as template items.
     *
     * @param \context_course $context The course context.
     * @param int $groupid Group id, or 0 for all enrolled users.
     * @param string $fieldlist User fields to fetch.
     * @param bool $viewfullnames Whether the current user may see full names.
     * @return array
     */
    private function build_user_items(\context_course $context, int $groupid, string $fieldlist,
            bool $viewfullnames): array {
        global $PAGE;

        $users = get_enrolled_users($context, '', $groupid, $fieldlist, '', 0, 0, true);
        $users = array_values($users);
        core_collator::asort_objects_by_property($users, $this->orderby, core_collator::SORT_NATURAL);

        $items = [];
        foreach ($users as $user) {
            $picture = new user_picture($user);
            $picture->size = 100;
            $items[] = [
                'fullname' => fullname($user, $viewfullnames),
                'picture' => $picture->get_url($PAGE)->out(false),
                'profileurl' => (new moodle_url('/user/view.php', [
                    'id' => $user->id,
                    'course' => $this->course->id,
                ]))->out(false),
            ];
        }

        return $items;
    }
}
