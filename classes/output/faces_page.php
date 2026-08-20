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

use block_faces\local\groups_helper;
use core_collator;
use core_user\fields;
use html_writer;
use moodle_url;
use renderable;
use renderer_base;
use single_select;
use templatable;
use user_picture;

/**
 * Renderable that shows the faces of course participants with filtering controls.
 *
 * @package   block_faces
 * @copyright 2025 Moodle
 * @license   http://www.gnu.org/copyleft/gpl.html GNU GPL v3 or later
 */
class faces_page implements renderable, templatable {

    /**
     * Constructor.
     *
     * @param \stdClass $course
     * @param int $groupid
     * @param string $orderby
     * @param array $groupids
     * @param bool $showfilters
     */
    public function __construct(
        private \stdClass $course,
        private int $groupid,
        private string $orderby,
        private array $groupids = [],
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
        global $USER;

        $context = \context_course::instance($this->course->id);
        $canseeall = groups_helper::can_see_all_participants($this->course, $context);
        $viewfullnames = has_capability('moodle/site:viewfullnames', $context);

        $validorders = ['firstname', 'lastname'];
        if (!in_array($this->orderby, $validorders, true)) {
            $this->orderby = 'firstname';
        }

        $validatedgroup = groups_helper::validate_group($this->course, $context, $this->groupid);
        $this->groupid = $validatedgroup ? (int)$validatedgroup->id : 0;

        $groupdata = groups_helper::prepare_group_selection($this->course, $context, $this->groupids);
        $selectedgroups = $groupdata['selectedgroups'];
        $selectedgroupids = $groupdata['selectedgroupids'];

        $orderurl = new moodle_url('/blocks/faces/showfaces/show.php', [
            'cid' => $this->course->id,
            'groupid' => $this->groupid,
        ]);
        if (!empty($selectedgroupids)) {
            groups_helper::apply_groupids_to_url($orderurl, $selectedgroupids);
        }
        $groupurl = new moodle_url('/blocks/faces/showfaces/show.php', [
            'cid' => $this->course->id,
            'orderby' => $this->orderby,
        ]);

        $orderoptions = [
            'firstname' => get_string('firstname', 'block_faces'),
            'lastname' => get_string('lastname', 'block_faces'),
        ];
        // Option 0 must always exist so the select reflects the current (default) view;
        // for restricted users it leads to the "own groups" view, not the full list.
        $groupoptions = [
            0 => $canseeall
                ? get_string('showallfaces', 'block_faces')
                : get_string('showmygroups', 'block_faces'),
        ];
        $groups = groups_get_all_groups($this->course->id, 0, 0, 'g.id, g.name');
        foreach ($groups as $group) {
            if (!groups_group_visible($group->id, $this->course)) {
                continue;
            }
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

        // In separate groups mode without the accessallgroups capability the user must
        // never see the full participant list: the default view (no group selected)
        // falls back to the groups the current user belongs to.
        $canlistall = $canseeall || $this->groupid > 0;
        $sectiongroups = $selectedgroups;
        if (empty($sectiongroups) && !$canlistall) {
            $sectiongroups = groups_get_all_groups($this->course->id, $USER->id);
        }

        $items = [];
        $sections = [];
        $displaysections = !empty($sectiongroups);
        if ($displaysections) {
            foreach ($sectiongroups as $group) {
                $sections[] = $this->build_group_section($group, $context, $fieldlist, $viewfullnames);
            }
        } else if ($canlistall) {
            $items = $this->build_user_items($context, $this->groupid, $fieldlist, $viewfullnames);
        }
        // Otherwise the user belongs to no visible group: $items stays empty and the
        // template renders the 'nousers' notice.

        $groupselection = [
            'title' => get_string('showfacesbygroup', 'block_faces'),
            'actionurl' => (new moodle_url('/blocks/faces/showfaces/show.php', [
                'cid' => $this->course->id,
            ]))->out(false),
            'formid' => html_writer::random_id('faces-groupselection-form'),
            'courseid' => $this->course->id,
            'orderby' => $this->orderby,
            'groupings' => $groupdata['groupings'],
            'hasgroupings' => $groupdata['hasgroupings'],
            'nogroupsavailable' => get_string('printnogroupsavailable', 'block_faces'),
            'submitlabel' => get_string('printapplyselection', 'block_faces'),
            'resetlabel' => get_string('printresetselection', 'block_faces'),
            'showreset' => !empty($selectedgroups),
            'reseturl' => (new moodle_url('/blocks/faces/showfaces/show.php', [
                'cid' => $this->course->id,
                'orderby' => $this->orderby,
                'groupid' => $this->groupid,
            ]))->out(false),
            'selectalllabel' => get_string('printselectallgroups', 'block_faces'),
            'deselectalllabel' => get_string('printdeselectallgroups', 'block_faces'),
            'contentid' => html_writer::random_id('faces-groupselection'),
            'togglelabel' => get_string('printtogglegroupselection', 'block_faces'),
            'expanded' => empty($selectedgroups),
        ];

        if (!empty($groupselection['groupings'])) {
            foreach ($groupselection['groupings'] as &$grouping) {
                $grouping['selectalllabel'] = $groupselection['selectalllabel'];
                $grouping['deselectalllabel'] = $groupselection['deselectalllabel'];
            }
            unset($grouping);
        }

        $templatecontext = [
            'coursename' => format_string($this->course->fullname, true, ['context' => $context]),
            'currentdate' => userdate(time(), get_string('strftimedate', 'langconfig')),
            'users' => $items,
            'hasusers' => !empty($items),
            'nousers' => get_string('nousers', 'block_faces'),
            'showfilters' => $this->showfilters,
            'printurl' => $this->build_print_url($selectedgroupids),
            'printlabel' => get_string('print', 'block_faces'),
            'isprint' => !$this->showfilters,
            'displaysections' => $displaysections,
            'sections' => $sections,
            'groupselection' => $groupselection,
            'showgroupselection' => true,
        ];

        if ($this->showfilters) {
            $templatecontext['orderbyselect'] = $output->render($orderselect);
            $templatecontext['groupselect'] = $output->render($groupselect);
        }

        return $templatecontext;
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

    /**
     * Build the print URL for the current selection.
     *
     * @param array $selectedgroupids Sanitised selected group ids.
     * @return string
     */
    private function build_print_url(array $selectedgroupids): string {
        $url = new moodle_url('/blocks/faces/print/page.php', [
            'cid' => $this->course->id,
            'orderby' => $this->orderby,
        ]);

        if (!empty($selectedgroupids)) {
            groups_helper::apply_groupids_to_url($url, $selectedgroupids);
        } else if ($this->groupid) {
            $url->param('groupid', $this->groupid);
        }

        return $url->out(false);
    }
}
